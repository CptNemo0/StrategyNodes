#include "kraken/l2_message_parser.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <charconv>
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/debug_log.h"
#include "core/decimal.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "core/spsc.h"
#include "core/unix_time.h"
#include "kraken/checksum.h"
#include "l2/book.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace kraken {

namespace {

using IsKind = bool (rapidjson::Value::*)() const;

// The member `name` of `object`, if `object` is an object that has it.
std::optional<rapidjson::Value::ConstMemberIterator> FindMember(
    const rapidjson::Value& object,
    std::string_view name) {
  if (!object.IsObject()) {
    return std::nullopt;
  }
  const rapidjson::Value::ConstMemberIterator member =
      object.FindMember(rapidjson::StringRef(
          name.data(), static_cast<rapidjson::SizeType>(name.size())));
  return member == object.MemberEnd() ? std::nullopt : std::optional{member};
}

// Thrown rather than letting rapidjson assert, so a frame whose layout Kraken
// changed fails the pipeline instead of aborting the process.
std::runtime_error MalformedField(std::string_view name) {
  return std::runtime_error{std::format(
      R"(Malformed Kraken book frame: missing or mistyped "{}")", name)};
}

// The member `name` of `object`, which must be of the kind `is_kind` checks
// for.
const rapidjson::Value& Require(const rapidjson::Value& object,
                                std::string_view name,
                                IsKind is_kind) {
  const std::optional<rapidjson::Value::ConstMemberIterator> member =
      FindMember(object, name);
  if (!member || !((*member)->value.*is_kind)()) {
    throw MalformedField(name);
  }
  return (*member)->value;
}

// The text of the optional member `name` of `object`, which must be a
// string if present.
std::optional<std::string_view> FindString(const rapidjson::Value& object,
                                           std::string_view name) {
  return FindMember(object, name)
      .transform([name](rapidjson::Value::ConstMemberIterator member) {
        if (!member->value.IsString()) {
          throw MalformedField(name);
        }
        return std::string_view{member->value.GetString(),
                                member->value.GetStringLength()};
      });
}

// Numbers arrive as strings because of kParseNumbersAsStringsFlag below.
i64 ParseScaled(const rapidjson::Value& level,
                std::string_view name,
                u32 decimals) {
  return core::ScaleDecimalStringToI64(
      Require(level, name, &rapidjson::Value::IsString).GetString(), decimals);
}

std::vector<l2::Message::Level> ParseSide(const rapidjson::Value& data,
                                          std::string_view name,
                                          const l2::PairPrecision& precision) {
  return Require(data, name, &rapidjson::Value::IsArray).GetArray() |
         std::views::transform([&precision](const rapidjson::Value& level) {
           return l2::Message::Level{
               .price = ParseScaled(level, "price", precision.price_decimals),
               .quantity = ParseScaled(level, "qty", precision.qty_decimals)};
         }) |
         std::ranges::to<std::vector>();
}

// Kraken's checksum exceeds the range of an i32 (e.g. 2520196449), so it is
// read as a u32.
u32 ParseChecksum(const rapidjson::Value& data) {
  const std::string_view text = FindString(data, "checksum").value_or("");
  u32 checksum{0};
  std::from_chars(text.data(), text.data() + text.size(), checksum);
  return checksum;
}

i64 ParseVenueTime(const rapidjson::Value& data) {
  return FindString(data, "timestamp")
      .and_then(core::ParseIso8601ToUnixNanos)
      .value_or(0);
}

// The single entry of a book frame's "data" array.
const rapidjson::Value& BookData(const rapidjson::Value& document) {
  const rapidjson::Value& data =
      Require(document, "data", &rapidjson::Value::IsArray);
  if (data.Empty() || !data[0].IsObject()) {
    throw std::runtime_error{
        R"(Malformed Kraken book frame: "data" holds no object)"};
  }
  return data[0];
}

}  // namespace

L2MessageParser::L2MessageParser(net::RawFrameQueue& frames,
                                 core::PipelineStatus& status,
                                 ChecksumMismatchHandler on_checksum_mismatch)
    : frames_{frames},
      status_{status},
      on_checksum_mismatch_{std::move(on_checksum_mismatch)} {}

L2MessageParser::~L2MessageParser() {
  Stop();
}

void L2MessageParser::Start() {
  {
    // A new connection starts every pair over with a snapshot; anything
    // before it would be left over from the last one.
    const std::scoped_lock lock{routes_mutex_};
    // flat_map hands out its values only through its iterators' proxies.
    for (auto&& [symbol, route] : routes_) {
      route.awaiting_snapshot = true;
    }
  }
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
                             u32 depth,
                             core::Receiver<l2::Message>& receiver) {
  const std::scoped_lock lock{routes_mutex_};
  if (!routes_
           .try_emplace(symbol, Route{.precision = precision,
                                      .receiver = &receiver,
                                      .book = l2::Book{depth}})
           .second) {
    throw std::invalid_argument{
        std::format("{} already has a receiver", symbol)};
  }
}

void L2MessageParser::Detach(std::string_view symbol) {
  const std::scoped_lock lock{routes_mutex_};
  const auto route = routes_.find(symbol);
  if (route != routes_.end()) {
    routes_.erase(route);
  }
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

  const rapidjson::Value& data = BookData(document);
  std::string symbol =
      Require(data, "symbol", &rapidjson::Value::IsString).GetString();
  const bool is_snapshot =
      std::string_view{
          Require(document, "type", &rapidjson::Value::IsString).GetString()} ==
      "snapshot";

  const std::scoped_lock lock{routes_mutex_};
  const auto route = routes_.find(symbol);
  // Unsubscribed, or not yet: the venue may still send a few frames.
  if (route == routes_.end()) {
    return;
  }
  Route& routed = route->second;
  // Updates before the route's first snapshot are left over from an earlier
  // subscription to the pair (e.g. at another depth) and do not apply to it.
  if (!is_snapshot && routed.awaiting_snapshot) {
    return;
  }
  if (std::exchange(routed.awaiting_snapshot, false)) {
    DEBUG_LOG("{}: snapshot received, streaming", route->first);
  }

  l2::Message message{.type = is_snapshot ? l2::Message::Type::kSnapshot
                                          : l2::Message::Type::kUpdate,
                      .checksum = ParseChecksum(data),
                      .venue_time_ns = ParseVenueTime(data),
                      .capture_time_ns = frame.capture_time_ns,
                      .symbol = std::move(symbol),
                      .buys = ParseSide(data, "bids", routed.precision),
                      .sells = ParseSide(data, "asks", routed.precision)};
  if (on_checksum_mismatch_) {
    routed.book.Apply(message);
    // Zero means the venue sent none, so there is nothing to check.
    if (const u32 expected = message.checksum; expected != 0) {
      if (const u32 actual = BookChecksum(routed.book); actual != expected) {
        std::println(stderr,
                     "{}: book checksum {} does not match Kraken's {}; "
                     "resubscribing",
                     route->first, actual, expected);
        routed.awaiting_snapshot = true;
        routed.book.Clear();
        routed.receiver->OnGap(core::Gap::kResubscribing);
        on_checksum_mismatch_(route->first);
        return;
      }
    }
  }
  routed.receiver->Receive(std::move(message));
}

}  // namespace kraken
