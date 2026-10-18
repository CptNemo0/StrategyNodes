#ifndef DATA_FEED_CRYPTO_CRC32_H_
#define DATA_FEED_CRYPTO_CRC32_H_

#include <string_view>

#include "core/aliasing.h"

namespace crypto {

u32 Crc32(std::string_view data);

}  // namespace crypto

#endif  // ! DATA_FEED_CRYPTO_CRC32_H_
