#include "kraken/tradable_pairs.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <boost/beast/http.hpp>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "kraken/constants.h"
#include "kraken/rest_error.h"
#include "net/https_client.h"

namespace kraken {

namespace {

constexpr std::string_view kOnlineStatus = "online";

}  // namespace

std::vector<std::string> FetchTradablePairs() {
  namespace http = boost::beast::http;

  const std::string target =
      std::format("{}?assetVersion=1", kAssetPairsEndpoint);

  net::HttpsClientOneShot::request<net::HttpsClientOneShot::string_body>
      request{http::verb::get, std::string_view{target}, 11};
  request.set(http::field::host, kRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");

  const net::HttpsClientOneShot::response<net::HttpsClientOneShot::string_body>
      result = net::HttpsClientOneShot{}.MakeRequest(request, kRestEndpoint,
                                                     kHttpsPort);

  rapidjson::Document document;
  document.Parse(result.body().c_str());

  ThrowOnRestError(document, "Errors when requesting asset pairs: ");

  const rapidjson::Value& result_object = document["result"];

  // Not result_object.GetObject(): on Windows, <windows.h> (pulled in via
  // Boost.Asio) macro-expands GetObject to GetObjectA/W.
  std::vector<std::string> symbols =
      std::ranges::subrange(result_object.MemberBegin(),
                            result_object.MemberEnd()) |
      std::views::filter([](const auto& pair) {
        return pair.value.HasMember("status") &&
               std::string_view{pair.value["status"].GetString()} ==
                   kOnlineStatus;
      }) |
      std::views::transform(
          [](const auto& pair) { return std::string{pair.name.GetString()}; }) |
      std::ranges::to<std::vector>();
  std::ranges::sort(symbols);
  return symbols;
}

}  // namespace kraken
