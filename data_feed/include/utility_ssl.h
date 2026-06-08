#ifndef DATA_FEED_UTILITY_SSL_H_
#define DATA_FEED_UTILITY_SSL_H_

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace data_feed {

bool IsValidBase64Character(char character);

std::string Base64Encode(std::span<const unsigned char> data);

std::vector<unsigned char> Base64Decode(std::string_view input);

std::vector<unsigned char> HmacSha512(std::span<const unsigned char> key,
                                      std::span<const unsigned char> message);

// SHA-256 digest of `data` as 32 raw bytes.
std::array<unsigned char, SHA256_DIGEST_LENGTH> Sha256(std::string_view data);

}  // namespace data_feed

#endif  // ! DATA_FEED_UTILITY_SSL_H_
