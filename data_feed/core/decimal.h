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

// How many fraction digits `value` needs, trailing zeros aside: 2 for "0.01"
// and "0.01000000", 0 for "1" and "10.". A step size given as decimal text
// (e.g. a venue's price increment) is a multiple of 10^-FractionDigits().
u32 FractionDigits(std::string_view value);

}  // namespace core

#endif  // ! DATA_FEED_CORE_DECIMAL_H_
