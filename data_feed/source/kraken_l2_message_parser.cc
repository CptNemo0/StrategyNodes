#include "kraken_l2_message_parser.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>

#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

#include "l2_message.h"
#include "utility.h"

namespace data_feed {

namespace {

std::vector<L2Message::Level> ParseSide(const rapidjson::Value& side) {
  return side.GetArray() |
         std::views::transform([](const rapidjson::Value& level) {
           return L2Message::Level{
               .price = double_string_to_i64(level["price"].GetString()),
               .quantity = double_string_to_i64(level["qty"].GetString())};
         }) |
         std::ranges::to<std::vector>();
}

}  // namespace

std::optional<L2Message> ParseKrakenL2Message(std::string_view frame) {
  rapidjson::Document document;
  // Numbers are kept as strings so double_string_to_i64 sees the exact
  // decimal text Kraken sent, without a round trip through double.
  document.Parse<rapidjson::kParseNumbersAsStringsFlag>(frame.data(),
                                                        frame.size());

  if (document.HasParseError() || !document.IsObject() ||
      !document.HasMember("channel") || document["channel"] != "book") {
    return std::nullopt;
  }

  const rapidjson::Value& data = document["data"][0];

  return L2Message{.type = document["type"] == "snapshot"
                               ? L2Message::Type::kSnapshot
                               : L2Message::Type::kUpdate,
                   .buys = ParseSide(data["bids"]),
                   .sells = ParseSide(data["asks"])};
}

}  // namespace data_feed
