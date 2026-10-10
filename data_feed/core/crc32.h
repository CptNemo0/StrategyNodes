#ifndef DATA_FEED_CORE_CRC32_H_
#define DATA_FEED_CORE_CRC32_H_

#include <string_view>

#include "core/aliasing.h"

namespace core {

u32 Crc32(std::string_view data);

}  // namespace core

#endif  // ! DATA_FEED_CORE_CRC32_H_
