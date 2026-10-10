#include "crypto/base64.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {

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

  // EVP_DecodeBlock counts padding as zero bytes. Counted on the trimmed text,
  // so noise after the padding (e.g. a trailing newline) does not hide it.
  std::size_t length = static_cast<std::size_t>(decoded);
  for (auto it = trimmed.rbegin(); it != trimmed.rend() && *it == '='; ++it) {
    --length;
  }

  out.resize(length);
  return out;
}

}  // namespace crypto
