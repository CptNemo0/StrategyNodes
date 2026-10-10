#include "kraken/websocket_token_generator.h"

#include <openssl/sha.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <array>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto/base64.h"
#include "crypto/hash.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/rest_error.h"
#include "kraken/websocket_token.h"
#include "net/https_client.h"

namespace kraken {

namespace {

constexpr std::string_view kApiKeyField = "API-Key";
constexpr std::string_view kApiSignatureField = "API-Sign";
constexpr std::string_view kHost = "api.kraken.com";

std::string GenerateNonce() {
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count());
}

std::string GenerateApiSignature(std::string_view private_key_b64,
                                 std::string_view path,
                                 std::string_view nonce,
                                 std::string_view body) {
  const std::array<unsigned char, SHA256_DIGEST_LENGTH> inner =
      crypto::Sha256(std::string(nonce).append(body));

  std::vector<unsigned char> message;
  message.reserve(path.size() + inner.size());
  message.insert(message.end(), path.begin(), path.end());
  message.insert(message.end(), inner.begin(), inner.end());

  return crypto::Base64Encode(
      crypto::HmacSha512(crypto::Base64Decode(private_key_b64), message));
}

}  // namespace

WebsocketTokenGenerator::WebsocketTokenGenerator(const Credentials& credentials)
    : credentials_(credentials) {}

std::unique_ptr<WebsocketToken> WebsocketTokenGenerator::GenerateToken() const {
  namespace http = boost::beast::http;

  const std::string nonce = GenerateNonce();
  const std::string body = std::format(R"({{"nonce":"{}"}})", nonce);
  const std::string signature = GenerateApiSignature(
      credentials_.private_key(), kTokenEndpoint, nonce, body);

  // Request is constructed with the final endpoint
  net::HttpsClientOneShot::request<net::HttpsClientOneShot::string_body>
      request{http::verb::post, kTokenEndpoint, 11};

  // Requests holds the base address of the host as a field
  request.set(http::field::host, kRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");
  request.set(http::field::content_type, net::kContentTypeJson);
  request.set(kApiKeyField, credentials_.public_key());
  request.set(kApiSignatureField, signature);
  request.body() = body.data();
  request.prepare_payload();

  // The host address is passed to `MakeRequest` to be resolved to IP by the
  // DNS.
  const net::HttpsClientOneShot::response<net::HttpsClientOneShot::string_body>
      result = net::HttpsClientOneShot{}.MakeRequest(request, kRestEndpoint,
                                                     kHttpsPort);

  rapidjson::Document json_document;
  json_document.Parse(result.body().c_str());

  ThrowOnRestError(json_document,
                   "Errors when requesting the websocket token: ");

  rapidjson::Value& result_object = json_document["result"];
  std::string token = std::move(result_object["token"].GetString());
  const int expires = result_object["expires"].GetInt();
  return std::make_unique<WebsocketToken>(std::move(token), expires);
}

}  // namespace kraken
