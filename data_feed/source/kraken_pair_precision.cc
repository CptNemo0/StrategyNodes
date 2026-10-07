#include "kraken_pair_precision.h"

#include <rapidjson/document.h>

#include <boost/beast/http.hpp>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>

#include "constants.h"
#include "https_client_one_shot.h"
#include "kraken_rest_error.h"

namespace data_feed {

namespace {

std::string JoinSymbols(std::span<const std::string> symbols) {
  return symbols | std::views::join_with(',') | std::ranges::to<std::string>();
}

}  // namespace

PairPrecisionMap FetchKrakenPairPrecision(
    std::span<const std::string> symbols) {
  namespace http = boost::beast::http;

  const std::string target = std::format(
      "{}?pair={}&assetVersion=1", kKrakenAssetPairsEndpoint,
      JoinSymbols(symbols));

  HttpsClientOneShot::request<HttpsClientOneShot::string_body> request{
      http::verb::get, std::string_view{target}, 11};
  request.set(http::field::host, kKrakenRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");

  const HttpsClientOneShot::response<HttpsClientOneShot::string_body> result =
      HttpsClientOneShot{}.MakeRequest(request, kKrakenRestEndpoint,
                                       kKrakenHttpsPort);

  rapidjson::Document document;
  document.Parse(result.body().c_str());

  ThrowOnKrakenRestError(document,
                        "Errors when requesting asset pair precision: ");

  const rapidjson::Value& result_object = document["result"];

  PairPrecisionMap precision;
  for (const std::string& symbol : symbols) {
    if (!result_object.HasMember(symbol.c_str())) {
      throw std::runtime_error{
          std::format("Kraken AssetPairs response is missing '{}'", symbol)};
    }
    const rapidjson::Value& pair = result_object[symbol.c_str()];
    precision.emplace(
        symbol, PairPrecision{.price_decimals = pair["pair_decimals"].GetUint(),
                              .qty_decimals = pair["lot_decimals"].GetUint()});
  }

  return precision;
}

}  // namespace data_feed
