#ifndef DATA_FEED_CORE_DECIMAL_H_
#define DATA_FEED_CORE_DECIMAL_H_

#include <string_view>

#include "core/aliasing.h"

namespace core {

// Converts a fixed-point decimal string (e.g. "2696.29") into an integer
// scaled by 10^decimals (e.g. 269629 for decimals=2), without a round trip
// through double. A fraction with fewer digits than `decimals` is
// zero-padded; a fraction with more is truncated.
i64 ScaleDecimalStringToI64(std::string_view value, u32 decimals);

}  // namespace core

#endif  // ! DATA_FEED_CORE_DECIMAL_H_
