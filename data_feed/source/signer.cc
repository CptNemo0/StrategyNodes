#include "signer.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "aliasing.h"
#include "credentials.h"

namespace data_feed {

namespace {

// source: https://docs.cdp.coinbase.com/exchange/websocket-feed/authentication
constexpr std::string_view kSignaturePath = "/users/self/verify";

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

}  // namespace

Signer::Signer(const Credentials& credentials) : credentials_(credentials) {}

Signer::Result Signer::GenerateSignature() const {
  const i64 timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

  const std::string message = std::format("{}GET{}", timestamp, kSignaturePath);

  const std::vector<unsigned char> hmac_key =
      Base64Decode(credentials_.secret());

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_len = 0;
  HMAC(EVP_sha256(), static_cast<const void*>(hmac_key.data()),
       static_cast<int>(hmac_key.size()),
       reinterpret_cast<const unsigned char*>(message.data()), message.size(),
       digest.data(), &digest_len);

  return Result{
      Base64Encode(std::span<const unsigned char>(digest.data(), digest_len)),
      std::to_string(timestamp)};
}

}  // namespace data_feed
