#ifndef DATA_FEED_CRYPTO_HASH_H_
#define DATA_FEED_CRYPTO_HASH_H_

#include <openssl/sha.h>

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace crypto {

// HMAC-SHA512 of `message` keyed by `key`, returned as raw bytes.
std::vector<unsigned char> HmacSha512(std::span<const unsigned char> key,
                                      std::span<const unsigned char> message);

// SHA-256 digest of `data` as 32 raw bytes.
std::array<unsigned char, SHA256_DIGEST_LENGTH> Sha256(std::string_view data);

}  // namespace crypto

#endif  // ! DATA_FEED_CRYPTO_HASH_H_
