#include "kraken_l2_message_parser.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <charconv>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "utility.h"

namespace data_feed {

namespace {

std::vector<L2Message::Level> ParseSide(const rapidjson::Value& side,
                                        const PairPrecision& precision) {
  return side.GetArray() |
         std::views::transform([&precision](const rapidjson::Value& level) {
           return L2Message::Level{
               .price = ScaleDecimalStringToI64(level["price"].GetString(),
                                                precision.price_decimals),
               .quantity = ScaleDecimalStringToI64(level["qty"].GetString(),
                                                   precision.qty_decimals)};
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
             ? ParseIso8601ToUnixNanos(data["timestamp"].GetString())
                   .value_or(0)
             : 0;
}

}  // namespace

std::optional<L2Message> ParseKrakenL2Message(
    std::string_view frame,
    i64 capture_time_ns,
    const PairPrecisionMap& precision) {
  rapidjson::Document document;
  // Numbers are kept as strings so ScaleDecimalStringToI64 sees the exact
  // decimal text Kraken sent, without a round trip through double.
  document.Parse<rapidjson::kParseNumbersAsStringsFlag>(frame.data(),
                                                        frame.size());

  if (document.HasParseError() || !document.IsObject() ||
      !document.HasMember("channel") || document["channel"] != "book") {
    return std::nullopt;
  }

  const rapidjson::Value& data = document["data"][0];
  std::string symbol = data["symbol"].GetString();
  const PairPrecision& pair_precision = precision.at(symbol);

  return L2Message{.type = document["type"] == "snapshot"
                               ? L2Message::Type::kSnapshot
                               : L2Message::Type::kUpdate,
                   .checksum = ParseChecksum(data),
                   .venue_time_ns = ParseVenueTime(data),
                   .capture_time_ns = capture_time_ns,
                   .symbol = std::move(symbol),
                   .buys = ParseSide(data["bids"], pair_precision),
                   .sells = ParseSide(data["asks"], pair_precision)};
}

}  // namespace data_feed
