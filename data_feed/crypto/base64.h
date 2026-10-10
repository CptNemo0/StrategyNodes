#ifndef DATA_FEED_CRYPTO_BASE64_H_
#define DATA_FEED_CRYPTO_BASE64_H_

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {

bool IsValidBase64Character(char character);

std::string Base64Encode(std::span<const unsigned char> data);

std::vector<unsigned char> Base64Decode(std::string_view input);

}  // namespace crypto

#endif  // ! DATA_FEED_CRYPTO_BASE64_H_
