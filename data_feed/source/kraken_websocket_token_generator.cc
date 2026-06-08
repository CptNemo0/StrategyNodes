#include "kraken_websocket_token_generator.h"

#include <openssl/sha.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <array>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <cassert>
#include <chrono>
#include <format>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "constants.h"
#include "https_client_one_shot.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token.h"
#include "utility_ssl.h"

namespace data_feed {

namespace {

constexpr std::string_view kKrakenApiKeyField = "API-Key";
constexpr std::string_view kKrakenApiSignatureField = "API-Sign";
constexpr std::string_view kKrakenHost = "api.kraken.com";

std::string GenerateKrakenNonce() {
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count());
}

std::string GenerateKrakenApiSignature(std::string_view private_key_b64,
                                       std::string_view path,
                                       std::string_view nonce,
                                       std::string_view body) {
  const std::array<unsigned char, SHA256_DIGEST_LENGTH> inner =
      Sha256(std::string(nonce).append(body));

  std::vector<unsigned char> message;
  message.reserve(path.size() + inner.size());
  message.insert(message.end(), path.begin(), path.end());
  message.insert(message.end(), inner.begin(), inner.end());

  return Base64Encode(HmacSha512(Base64Decode(private_key_b64), message));
}

}  // namespace

KrakenWebsocketTokenGenerator::KrakenWebsocketTokenGenerator(
    const KrakenCredentials& credentials)
    : credentials_(credentials) {}

std::unique_ptr<KrakenWebsocketToken>
KrakenWebsocketTokenGenerator::GenerateToken() const {
  namespace http = boost::beast::http;

  const std::string nonce = GenerateKrakenNonce();
  const std::string body = std::format(R"({{"nonce":"{}"}})", nonce);
  const std::string signature = GenerateKrakenApiSignature(
      credentials_.private_key(), kKrakenTokenEndpoint, nonce, body);

  // Request is constructed with the final endpoint
  HttpsClientOneShot::request<HttpsClientOneShot::string_body> request{
      http::verb::post, kKrakenTokenEndpoint, 11};

  // Requests holds the base address of the host as a field
  request.set(http::field::host, kKrakenRestEndpoint);
  request.set(http::field::user_agent, "app/0.1");
  request.set(http::field::content_type, kContentTypeJson);
  request.set(kKrakenApiKeyField, credentials_.public_key());
  request.set(kKrakenApiSignatureField, signature);
  request.body() = body.data();
  request.prepare_payload();

  // The host address is passed to `MakeRequest` to be resolved to IP by the
  // DNS.
  const HttpsClientOneShot::response<HttpsClientOneShot::string_body> result =
      HttpsClientOneShot{}.MakeRequest(request, kKrakenRestEndpoint,
                                       kKrakenHttpsPort);

  rapidjson::Document json_document;
  json_document.Parse(result.body().c_str());

  const rapidjson::Value& error_array = json_document["error"];
  assert(error_array.IsArray());
  if (!error_array.Empty()) {
    std::stringstream error_stream{};
    error_stream << "Errors when requesting the websocket token: ";

    for (auto i{0uz}; i < error_array.Size(); ++i) {
      error_stream << (error_array[i].IsString()
                           ? std::format("\t{}\n", error_array[i].GetString())
                           : "\t[empty error field]\n");
    }
    throw std::runtime_error{std::move(error_stream.str())};
  }

  rapidjson::Value& result_object = json_document["result"];
  std::string token = std::move(result_object["token"].GetString());
  const int expires = result_object["expires"].GetInt();
  return std::make_unique<KrakenWebsocketToken>(std::move(token), expires);
}

}  // namespace data_feed
