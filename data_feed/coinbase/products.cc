#include "coinbase/products.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <boost/beast/http.hpp>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "coinbase/constants.h"
#include "core/decimal.h"
#include "core/json.h"
#include "l2/pair_precision.h"
#include "net/https_client.h"

namespace coinbase {

namespace {

namespace http = boost::beast::http;

constexpr std::string_view kOnlineStatus = "online";

// The JSON object `body` holds. Throws if it holds none.
rapidjson::Document ParseObject(std::string_view body) {
  rapidjson::Document document;
  document.Parse(body.data(), body.size());
  if (document.HasParseError() || !document.IsObject()) {
    throw std::runtime_error{std::format(
        "Coinbase answered with no JSON object: {}", body.substr(0, 200))};
  }
  return document;
}

// Whether the optional boolean `name` of `product` is true.
bool IsSet(const rapidjson::Value& product, std::string_view name) {
  const std::optional<rapidjson::Value::ConstMemberIterator> member =
      core::FindJsonMember(product, name);
  return member && (*member)->value.IsBool() && (*member)->value.GetBool();
}

// Why `product` cannot be streamed, or nothing if it can.
std::optional<std::string> WhyUnstreamable(const rapidjson::Value& product) {
  const std::string_view id = core::RequireJsonString(product, "product_id");
  if (const std::string_view status =
          core::FindJsonString(product, "status").value_or("");
      status != kOnlineStatus) {
    return std::format("Coinbase lists {} as '{}', not '{}'", id, status,
                       kOnlineStatus);
  }
  if (IsSet(product, "is_disabled") || IsSet(product, "trading_disabled")) {
    return std::format("Coinbase has disabled trading in {}", id);
  }
  // E.g. BTC-USDC, whose public data comes as BTC-USD's: a subscription to it
  // would never see a frame under its own id.
  if (const std::string_view alias =
          core::FindJsonString(product, "alias").value_or("");
      !alias.empty()) {
    return std::format("Coinbase streams {} as {}; subscribe to that", id,
                       alias);
  }
  return std::nullopt;
}

// GETs `target` from Coinbase's REST host.
net::HttpsClientOneShot::Result Get(const std::string& target) {
  net::HttpsClientOneShot::request<net::HttpsClientOneShot::string_body>
      request{http::verb::get, std::string_view{target}, 11};
  request.set(http::field::host, kRestHost);
  request.set(http::field::user_agent, "app/0.1");
  return net::HttpsClientOneShot{}.MakeRequest(request, kRestHost, kHttpsPort);
}

void ThrowUnlessOk(const net::HttpsClientOneShot::Result& response,
                   std::string_view target) {
  if (response.result() != http::status::ok) {
    throw std::runtime_error{std::format(
        "Coinbase answered {} with HTTP {}: {}", target, response.result_int(),
        std::string_view{response.body()}.substr(0, 200))};
  }
}

}  // namespace

l2::PairPrecision ParseProductPrecision(std::string_view body) {
  const rapidjson::Document product = ParseObject(body);
  if (const std::optional<std::string> why = WhyUnstreamable(product)) {
    throw UnknownProductError{*why};
  }
  // The price step, which quote_increment stands in for on products without
  // one.
  const std::string_view price_increment =
      core::FindJsonString(product, "price_increment").value_or("");
  return l2::PairPrecision{
      .price_decimals = core::FractionDigits(
          price_increment.empty()
              ? core::RequireJsonString(product, "quote_increment")
              : price_increment),
      .qty_decimals = core::FractionDigits(
          core::RequireJsonString(product, "base_increment"))};
}

std::vector<std::string> ParseTradableProducts(std::string_view body) {
  const rapidjson::Document document = ParseObject(body);
  std::vector<std::string> ids =
      core::RequireJsonMember(document, "products", &rapidjson::Value::IsArray)
          .GetArray() |
      std::views::filter([](const rapidjson::Value& product) {
        return !WhyUnstreamable(product);
      }) |
      std::views::transform([](const rapidjson::Value& product) {
        return std::string{core::RequireJsonString(product, "product_id")};
      }) |
      std::ranges::to<std::vector>();
  std::ranges::sort(ids);
  return ids;
}

l2::PairPrecision FetchProductPrecision(std::string_view product_id) {
  const std::string target =
      std::format("{}/{}", kProductsEndpoint, product_id);
  const net::HttpsClientOneShot::Result response = Get(target);
  if (response.result() == http::status::not_found ||
      response.result() == http::status::bad_request) {
    throw UnknownProductError{
        std::format("Coinbase does not list {}", product_id)};
  }
  ThrowUnlessOk(response, target);
  return ParseProductPrecision(response.body());
}

std::vector<std::string> FetchTradableProducts() {
  const std::string target =
      std::format("{}?product_type=SPOT", kProductsEndpoint);
  const net::HttpsClientOneShot::Result response = Get(target);
  ThrowUnlessOk(response, target);
  return ParseTradableProducts(response.body());
}

}  // namespace coinbase
