#include "kraken_tradable_pairs.h"

#include <rapidjson/document.h>

#include <boost/beast/http.hpp>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "constants.h"
#include "https_client_one_shot.h"
#include "kraken_rest_error.h"

namespace data_feed {

namespace {

constexpr std::string_view kOnlineStatus = "online";

}  // namespace

std::vector<std::string> FetchKrakenTradablePairs() {
  namespace http = boost::beast::http;

  const std::string target =
      std::format("{}?assetVersion=1", kKrakenAssetPairsEndpoint);

  HttpsClientOneShot::request<HttpsClientOneShot::string_body> request{
      http::verb::get, std::string_view{target}, 11};
  request.set(http::field::host, kKrakenRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");

  const HttpsClientOneShot::response<HttpsClientOneShot::string_body> result =
      HttpsClientOneShot{}.MakeRequest(request, kKrakenRestEndpoint,
                                       kKrakenHttpsPort);

  rapidjson::Document document;
  document.Parse(result.body().c_str());

  ThrowOnKrakenRestError(document, "Errors when requesting asset pairs: ");

  const rapidjson::Value& result_object = document["result"];

  // Not result_object.GetObject(): on Windows, <windows.h> (pulled in via
  // Boost.Asio) macro-expands GetObject to GetObjectA/W.
  return std::ranges::subrange(result_object.MemberBegin(),
                               result_object.MemberEnd()) |
         std::views::filter([](const auto& pair) {
           return pair.value.HasMember("status") &&
                  std::string_view{pair.value["status"].GetString()} ==
                      kOnlineStatus;
         }) |
         std::views::transform([](const auto& pair) {
           return std::string{pair.name.GetString()};
         }) |
         std::ranges::to<std::vector>();
}

}  // namespace data_feed
