#include "kraken/l2_decoder.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <charconv>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/aliasing.h"
#include "core/decimal.h"
#include "core/json.h"
#include "core/unix_time.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace kraken {

namespace {

// Numbers arrive as strings because of kParseNumbersAsStringsFlag below.
i64 ParseScaled(const rapidjson::Value& level,
                std::string_view name,
                u32 decimals) {
  return core::ScaleDecimalStringToI64(core::RequireJsonString(level, name),
                                       decimals);
}

std::vector<l2::Message::Level> ParseSide(const rapidjson::Value& data,
                                          std::string_view name,
                                          const l2::PairPrecision& precision) {
  return core::RequireJsonMember(data, name, &rapidjson::Value::IsArray)
             .GetArray() |
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
  const std::string_view text =
      core::FindJsonString(data, "checksum").value_or("");
  u32 checksum{0};
  std::from_chars(text.data(), text.data() + text.size(), checksum);
  return checksum;
}

i64 ParseVenueTime(const rapidjson::Value& data) {
  return core::FindJsonString(data, "timestamp")
      .and_then(core::ParseIso8601ToUnixNanos)
      .value_or(0);
}

// The single entry of a book frame's "data" array.
const rapidjson::Value& BookData(const rapidjson::Value& document) {
  const rapidjson::Value& data =
      core::RequireJsonMember(document, "data", &rapidjson::Value::IsArray);
  if (data.Empty() || !data[0].IsObject()) {
    throw std::runtime_error{
        R"(Malformed Kraken book frame: "data" holds no object)"};
  }
  return data[0];
}

}  // namespace

void L2Decoder::Decode(const net::RawFrame& frame, Output& output) {
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
  const std::string_view symbol = core::RequireJsonString(data, "symbol");
  const l2::Message::Type type =
      core::RequireJsonString(document, "type") == "snapshot"
          ? l2::Message::Type::kSnapshot
          : l2::Message::Type::kUpdate;

  const l2::PairPrecision* precision = output.Find(symbol, type);
  if (precision == nullptr) {
    return;
  }
  output.Emit(l2::Message{.type = type,
                          .checksum = ParseChecksum(data),
                          .venue_time_ns = ParseVenueTime(data),
                          .capture_time_ns = frame.capture_time_ns,
                          .symbol = std::string{symbol},
                          .buys = ParseSide(data, "bids", *precision),
                          .sells = ParseSide(data, "asks", *precision)});
}

}  // namespace kraken
