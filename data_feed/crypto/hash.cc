#include "crypto/hash.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <array>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace crypto {

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

}  // namespace crypto
