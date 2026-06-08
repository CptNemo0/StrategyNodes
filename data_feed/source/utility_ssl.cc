#include "utility_ssl.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace data_feed {

bool IsValidBase64Character(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') ||
         (character == '+' || character == '/' || character == '=');
}

std::string Base64Encode(std::span<const unsigned char> data) {
  if (data.empty()) {
    return {};
  }

  std::string out(4 * ((data.size() + 2) / 3), '\0');

  const int written =
      EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(),
                      static_cast<int>(data.size()));

  out.resize(static_cast<std::size_t>(written));
  return out;
}

std::vector<unsigned char> Base64Decode(std::string_view input) {
  if (input.empty()) {
    return {};
  }

  std::string trimmed;
  trimmed.reserve(input.size());

  std::ranges::for_each(input | std::views::filter(IsValidBase64Character),
                        [&](char c) { trimmed.push_back(c); });

  if (trimmed.size() % 4) {
    throw std::runtime_error("Length of base64 string not divisible by 4.");
  }

  std::vector<unsigned char> out(3 * (trimmed.size() / 4));

  const int decoded = EVP_DecodeBlock(
      out.data(), reinterpret_cast<const unsigned char*>(trimmed.data()),
      static_cast<int>(trimmed.size()));

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

std::array<unsigned char, SHA256_DIGEST_LENGTH> Sha256(std::string_view data) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  unsigned int length = 0;
  if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha256(),
                 nullptr) != 1) {
    throw std::runtime_error("SHA-256 digest failed");
  }
  return digest;
}

}  // namespace data_feed
