#include "coinbase/l2_decoder.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <charconv>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/decimal.h"
#include "core/json.h"
#include "core/unix_time.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "venue/l2_decoder.h"

namespace coinbase {

namespace {

// The unsigned integer `name` of `object`. Numbers arrive as strings because
// of kParseNumbersAsStringsFlag below; Coinbase sends some as strings anyway.
u64 RequireUnsigned(const rapidjson::Value& object, std::string_view name) {
  const std::string_view text = core::RequireJsonString(object, name);
  u64 value{0};
  const std::from_chars_result result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    throw core::MalformedJsonField(name);
  }
  return value;
}

// The decimal `name` of `level`, scaled at `decimals`. Throws if it is
// negative or finer than `decimals` allow, as no level of the product can be.
i64 RequireScaled(const rapidjson::Value& level,
                  std::string_view name,
                  u32 decimals) {
  const std::string_view text = core::RequireJsonString(level, name);
  if (text.starts_with('-')) {
    throw std::runtime_error{
        std::format(R"(Coinbase sent a negative "{}": {})", name, text)};
  }
  if (core::FractionDigits(text) > decimals) {
    throw std::runtime_error{std::format(
        R"(Coinbase sent a "{}" of {}, finer than the product's {} decimals; )"
        "its increments must have changed",
        name, text, decimals)};
  }
  return core::ScaleDecimalStringToI64(text, decimals);
}

l2::Message::Type ParseType(std::string_view type) {
  if (type != "snapshot" && type != "update") {
    throw std::runtime_error{
        std::format("Coinbase sent a level2 event of type '{}'", type)};
  }
  return type == "snapshot" ? l2::Message::Type::kSnapshot
                            : l2::Message::Type::kUpdate;
}

// The side of `message` a level of `side` belongs on.
std::vector<l2::Message::Level>& SideOf(l2::Message& message,
                                        std::string_view side) {
  if (side != "bid" && side != "offer") {
    throw std::runtime_error{
        std::format("Coinbase sent a level on side '{}'", side)};
  }
  return side == "bid" ? message.buys : message.sells;
}

// Emits the book change one "l2_data" event holds, if `output` wants it.
void DecodeEvent(const rapidjson::Value& event,
                 i64 venue_time_ns,
                 i64 capture_time_ns,
                 venue::L2Decoder::Output& output) {
  const std::string_view product = core::RequireJsonString(event, "product_id");
  const l2::Message::Type type =
      ParseType(core::RequireJsonString(event, "type"));
  const l2::PairPrecision* precision = output.Find(product, type);
  if (precision == nullptr) {
    return;
  }
  l2::Message message{.type = type,
                      .checksum = 0,
                      .venue_time_ns = venue_time_ns,
                      .capture_time_ns = capture_time_ns,
                      .symbol = std::string{product},
                      .buys = {},
                      .sells = {}};
  for (const rapidjson::Value& update :
       core::RequireJsonMember(event, "updates", &rapidjson::Value::IsArray)
           .GetArray()) {
    SideOf(message, core::RequireJsonString(update, "side"))
        .push_back(l2::Message::Level{
            .price =
                RequireScaled(update, "price_level", precision->price_decimals),
            .quantity = RequireScaled(update, "new_quantity",
                                      precision->qty_decimals)});
  }
  output.Emit(std::move(message));
}

}  // namespace

void L2Decoder::Decode(const net::RawFrame& frame, Output& output) {
  rapidjson::Document document;
  // Numbers are kept as strings so ScaleDecimalStringToI64 sees the exact
  // decimal text Coinbase sent, without a round trip through double.
  document.Parse<rapidjson::kParseNumbersAsStringsFlag>(frame.json.data(),
                                                        frame.json.size());
  if (document.HasParseError() || !document.IsObject()) {
    return;
  }
  if (core::FindJsonString(document, "type") == "error") {
    std::println(stderr, "Coinbase reported an error: {}", frame.json);
    return;
  }
  const std::optional<std::string_view> channel =
      core::FindJsonString(document, "channel");
  if (!channel ||
      !Advance(last_sequence_, RequireUnsigned(document, "sequence_num"),
               "sequence numbers")) {
    return;
  }

  const rapidjson::Value::ConstArray events =
      core::RequireJsonMember(document, "events", &rapidjson::Value::IsArray)
          .GetArray();
  if (*channel == "heartbeats") {
    for (const rapidjson::Value& heartbeat : events) {
      Advance(last_heartbeat_, RequireUnsigned(heartbeat, "heartbeat_counter"),
              "heartbeats");
    }
    return;
  }
  if (*channel != "l2_data") {
    return;
  }
  // When Coinbase sent the frame; each level's event_time is when it changed.
  const i64 venue_time_ns = core::FindJsonString(document, "timestamp")
                                .and_then(core::ParseIso8601ToUnixNanos)
                                .value_or(0);
  for (const rapidjson::Value& event : events) {
    DecodeEvent(event, venue_time_ns, frame.capture_time_ns, output);
  }
}

bool L2Decoder::Advance(std::optional<u64>& last,
                        u64 next,
                        std::string_view what) {
  if (last && next <= *last) {
    return false;
  }
  if (last && next != *last + 1) {
    throw std::runtime_error{
        std::format("Coinbase skipped {} {} to {}; reconnecting for fresh "
                    "snapshots",
                    what, *last + 1, next - 1)};
  }
  last = next;
  return true;
}

}  // namespace coinbase
