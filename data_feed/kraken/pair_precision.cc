#include "kraken/pair_precision.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <boost/beast/http.hpp>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "kraken/constants.h"
#include "kraken/rest_error.h"
#include "net/https_client.h"

namespace kraken {

namespace {

// The error Kraken answers a query for a pair it does not list with.
constexpr std::string_view kUnknownPairError = "EQuery:Unknown asset pair";

std::string JoinSymbols(std::span<const std::string> symbols) {
  return symbols | std::views::join_with(',') | std::ranges::to<std::string>();
}

// Whether Kraken's error envelope says one of the requested pairs is unknown.
bool ReportsUnknownPair(const rapidjson::Document& document) {
  if (!document.IsObject()) {
    return false;
  }
  const rapidjson::Value::ConstMemberIterator errors =
      document.FindMember("error");
  return errors != document.MemberEnd() && errors->value.IsArray() &&
         std::ranges::any_of(
             errors->value.GetArray(), [](const rapidjson::Value& error) {
               return error.IsString() &&
                      std::string_view{error.GetString()}.starts_with(
                          kUnknownPairError);
             });
}

}  // namespace

l2::PairPrecisionMap FetchPairPrecision(std::span<const std::string> symbols) {
  namespace http = boost::beast::http;

  const std::string target = std::format(
      "{}?pair={}&assetVersion=1", kAssetPairsEndpoint, JoinSymbols(symbols));

  net::HttpsClientOneShot::request<net::HttpsClientOneShot::string_body>
      request{http::verb::get, std::string_view{target}, 11};
  request.set(http::field::host, kRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");

  const net::HttpsClientOneShot::response<net::HttpsClientOneShot::string_body>
      result = net::HttpsClientOneShot{}.MakeRequest(request, kRestEndpoint,
                                                     kHttpsPort);

  rapidjson::Document document;
  document.Parse(result.body().c_str());

  if (ReportsUnknownPair(document)) {
    throw UnknownPairError{
        std::format("Kraken does not list {}", JoinSymbols(symbols))};
  }
  ThrowOnRestError(document, "Errors when requesting asset pair precision: ");

  const rapidjson::Value& result_object = document["result"];

  l2::PairPrecisionMap precision;
  for (const std::string& symbol : symbols) {
    if (!result_object.HasMember(symbol.c_str())) {
      throw UnknownPairError{
          std::format("Kraken AssetPairs response is missing '{}'", symbol)};
    }
    const rapidjson::Value& pair = result_object[symbol.c_str()];
    precision.emplace(
        symbol,
        l2::PairPrecision{.price_decimals = pair["pair_decimals"].GetUint(),
                          .qty_decimals = pair["lot_decimals"].GetUint()});
  }

  return precision;
}

}  // namespace kraken
