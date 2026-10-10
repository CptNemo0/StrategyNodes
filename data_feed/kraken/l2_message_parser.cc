#include "kraken/l2_message_parser.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <charconv>
#include <mutex>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/decimal.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "core/spsc.h"
#include "core/unix_time.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace kraken {

namespace {

std::vector<l2::Message::Level> ParseSide(const rapidjson::Value& side,
                                          const l2::PairPrecision& precision) {
  return side.GetArray() |
         std::views::transform([&precision](const rapidjson::Value& level) {
           return l2::Message::Level{
               .price = core::ScaleDecimalStringToI64(
                   level["price"].GetString(), precision.price_decimals),
               .quantity = core::ScaleDecimalStringToI64(
                   level["qty"].GetString(), precision.qty_decimals)};
         }) |
         std::ranges::to<std::vector>();
}

// Kraken's checksum exceeds the range of an i32 (e.g. 2520196449), so it is
// read as a u32. Numbers arrive as strings because of
// kParseNumbersAsStringsFlag below.
u32 ParseChecksum(const rapidjson::Value& data) {
  if (!data.HasMember("checksum")) {
    return 0;
  }

  const std::string_view text = data["checksum"].GetString();
  u32 checksum{0};
  std::from_chars(text.data(), text.data() + text.size(), checksum);
  return checksum;
}

i64 ParseVenueTime(const rapidjson::Value& data) {
  return data.HasMember("timestamp")
             ? core::ParseIso8601ToUnixNanos(data["timestamp"].GetString())
                   .value_or(0)
             : 0;
}

}  // namespace

L2MessageParser::L2MessageParser(net::RawFrameQueue& frames,
                                 core::PipelineStatus& status)
    : frames_{frames}, status_{status} {}

L2MessageParser::~L2MessageParser() {
  Stop();
}

void L2MessageParser::Start() {
  thread_ = std::jthread{[this](const std::stop_token& stop) {
    core::Drain(
        stop, "parser", frames_,
        [this](const net::RawFrame& frame) { Dispatch(frame); },
        [this] { status_.Fail(); });
  }};
}

void L2MessageParser::Stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void L2MessageParser::Attach(const std::string& symbol,
                             const l2::PairPrecision& precision,
                             core::Receiver<l2::Message>& receiver) {
  const std::scoped_lock lock{routes_mutex_};
  routes_.insert_or_assign(
      symbol, Route{.precision = precision, .receiver = &receiver});
}

void L2MessageParser::Detach(std::string_view symbol) {
  const std::scoped_lock lock{routes_mutex_};
  routes_.erase(symbol);
}

void L2MessageParser::Dispatch(const net::RawFrame& frame) {
  rapidjson::Document document;
  // Numbers are kept as strings so ScaleDecimalStringToI64 sees the exact
  // decimal text Kraken sent, without a round trip through double.
  document.Parse<rapidjson::kParseNumbersAsStringsFlag>(frame.json.data(),
                                                        frame.json.size());

  if (document.HasParseError() || !document.IsObject() ||
      !document.HasMember("channel") || document["channel"] != "book") {
    return;
  }

  const rapidjson::Value& data = document["data"][0];
  std::string symbol = data["symbol"].GetString();
  const bool is_snapshot = document["type"] == "snapshot";

  const std::scoped_lock lock{routes_mutex_};
  const auto route = routes_.find(symbol);
  // Unsubscribed, or not yet: the venue may still send a few frames.
  if (route == routes_.end()) {
    return;
  }
  // Updates before the route's first snapshot are left over from an earlier
  // subscription to the pair (e.g. at another depth) and do not apply to it.
  if (!is_snapshot && route->second.awaiting_snapshot) {
    return;
  }
  route->second.awaiting_snapshot = false;

  const l2::PairPrecision& precision = route->second.precision;
  route->second.receiver->Receive(
      l2::Message{.type = is_snapshot ? l2::Message::Type::kSnapshot
                                      : l2::Message::Type::kUpdate,
                  .checksum = ParseChecksum(data),
                  .venue_time_ns = ParseVenueTime(data),
                  .capture_time_ns = frame.capture_time_ns,
                  .symbol = std::move(symbol),
                  .buys = ParseSide(data["bids"], precision),
                  .sells = ParseSide(data["asks"], precision)});
}

}  // namespace kraken
