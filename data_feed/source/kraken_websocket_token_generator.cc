#include "kraken_websocket_token_generator.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/tls1.h>

#include <array>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "constants.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token.h"
#include "tls_context.h"

namespace data_feed {

namespace {

constexpr std::string_view kKrakenHost = "api.kraken.com";
constexpr std::string_view kKrakenHttpsPort = "443";

// Base64-encodes `data` using OpenSSL's EVP_EncodeBlock.
std::string Base64Encode(std::span<const unsigned char> data) {
  if (data.empty()) {
    return {};
  }
  // EVP_EncodeBlock writes 4 characters per 3 input bytes, plus a NUL.
  std::string out(4 * ((data.size() + 2) / 3), '\0');
  const int written =
      EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(),
                      static_cast<int>(data.size()));
  out.resize(static_cast<std::size_t>(written));
  return out;
}

// Base64-decodes `input`, which must be standard base64 (length a multiple of
// 4). Throws std::runtime_error on malformed input.
std::vector<unsigned char> Base64Decode(std::string_view input) {
  if (input.empty()) {
    return {};
  }
  // EVP_DecodeBlock decodes whole 4-character groups into 3 bytes each,
  // counting '=' padding as zero bytes; we trim those to recover the true
  // length.
  std::vector<unsigned char> out(3 * (input.size() / 4));
  const int decoded = EVP_DecodeBlock(
      out.data(), reinterpret_cast<const unsigned char*>(input.data()),
      static_cast<int>(input.size()));
  if (decoded < 0) {
    throw std::runtime_error("Failed to base64-decode API secret");
  }

  std::size_t length = static_cast<std::size_t>(decoded);
  for (auto it = input.rbegin(); it != input.rend() && *it == '='; ++it) {
    --length;
  }
  out.resize(length);
  return out;
}

// SHA-256 digest of `data` as 32 raw bytes.
std::array<unsigned char, SHA256_DIGEST_LENGTH> Sha256(std::string_view data) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  unsigned int length = 0;
  if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha256(),
                 nullptr) != 1) {
    throw std::runtime_error("SHA-256 digest failed");
  }
  return digest;
}

// HMAC-SHA512 of `message` keyed by `key`, returned as raw bytes.
std::vector<unsigned char> HmacSha512(std::span<const unsigned char> key,
                                      std::span<const unsigned char> message) {
  std::vector<unsigned char> out(EVP_MAX_MD_SIZE);
  unsigned int length = 0;
  if (HMAC(EVP_sha512(), key.data(), static_cast<int>(key.size()),
           message.data(), message.size(), out.data(), &length) == nullptr) {
    throw std::runtime_error("HMAC-SHA512 failed");
  }
  out.resize(length);
  return out;
}

// Kraken nonce: milliseconds since the Unix epoch, as a decimal string. Must
// strictly increase across calls for a given key.
std::string Nonce() {
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count());
}

// Computes the Kraken REST API-Sign value:
//   base64( HMAC-SHA512( base64decode(secret), path || SHA256(nonce + body) ) )
// `body` is the exact bytes sent as the POST payload, so the signature stays in
// sync with what the server re-hashes.
std::string SignRequest(std::string_view private_key_b64,
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

// Issues a one-shot HTTPS POST to Kraken and returns the response body. Opens a
// fresh TLS connection per call; token requests are infrequent, so this avoids
// holding connection state on the generator.
std::string HttpsPost(std::string_view target,
                      std::string_view body,
                      std::string_view api_key,
                      std::string_view api_sign) {
  namespace beast = boost::beast;
  namespace http = boost::beast::http;
  namespace asio = boost::asio;
  using tcp = asio::ip::tcp;

  asio::io_context ioc;
  // TlsContext loads the platform trust store (the Windows ROOT store via
  // crypt32) and enables peer verification; a bare set_default_verify_paths()
  // finds no CA bundle on Windows, so the handshake would fail to verify.
  TlsContext tls;

  const std::string host(kKrakenHost);
  beast::ssl_stream<beast::tcp_stream> stream(ioc, tls.native());

  // SNI must be set before the handshake (sent inside the ClientHello).
  if (SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
    throw std::runtime_error("Failed to set TLS SNI hostname");
  }

  tcp::resolver resolver(ioc);
  beast::get_lowest_layer(stream).connect(
      resolver.resolve(host, kKrakenHttpsPort));
  stream.handshake(asio::ssl::stream_base::client);

  http::request<http::string_body> req{http::verb::post, target, 11};
  req.set(http::field::host, host);
  req.set(http::field::user_agent, "data_feed/0.1");
  req.set(http::field::content_type, "application/json");
  req.set("API-Key", api_key);
  req.set("API-Sign", api_sign);
  req.body() = std::string(body);
  req.prepare_payload();

  http::write(stream, req);

  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(stream, buffer, res);

  // Best-effort TLS close. Servers frequently close abruptly after replying;
  // the full response is already read, so a non-clean shutdown is not fatal.
  beast::error_code ec;
  stream.shutdown(ec);

  return std::move(res.body());
}

// Pulls the "token" value out of Kraken's {"error":[...],"result":{...}}
// envelope. A non-empty error array means the request was rejected.
std::string ExtractToken(std::string_view response) {
  constexpr std::string_view kErrorKey = "\"error\":[";
  const std::size_t error_pos = response.find(kErrorKey);
  if (error_pos != std::string_view::npos) {
    const std::size_t open = error_pos + kErrorKey.size();
    const std::size_t close = response.find(']', open);
    if (close != std::string_view::npos && close > open) {
      throw std::runtime_error(
          std::format("Kraken GetWebSocketsToken error: {}",
                      response.substr(open, close - open)));
    }
  }

  constexpr std::string_view kTokenKey = "\"token\":\"";
  const std::size_t start = response.find(kTokenKey);
  if (start == std::string_view::npos) {
    throw std::runtime_error(
        std::format("No token in Kraken response: {}", response));
  }
  const std::size_t value = start + kTokenKey.size();
  const std::size_t end = response.find('"', value);
  if (end == std::string_view::npos) {
    throw std::runtime_error("Malformed token field in Kraken response");
  }
  return std::string(response.substr(value, end - value));
}

}  // namespace

KrakenWebsocketTokenGenerator::KrakenWebsocketTokenGenerator(
    const KrakenCredentials& credentials)
    : credentials_(credentials) {}

std::unique_ptr<KrakenWebsocketToken>
KrakenWebsocketTokenGenerator::GenerateToken() const {
  const std::string nonce = Nonce();
  const std::string body = std::format(R"({{"nonce":"{}"}})", nonce);

  return std::unique_ptr<KrakenWebsocketToken>(
      new KrakenWebsocketToken{ExtractToken(
          HttpsPost(kKrakenTokenEndpoint, body, credentials_.public_key(),
                    SignRequest(credentials_.private_key(),
                                kKrakenTokenEndpoint, nonce, body)))});
}

}  // namespace data_feed
