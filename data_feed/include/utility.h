#ifndef DATA_FEED_UTILITY_H_
#define DATA_FEED_UTILITY_H_

#include <optional>
#include <string>
#include <string_view>

#include "aliasing.h"

namespace data_feed {

struct EnvironmentVarNameValue {
  std::string_view name;
  std::optional<std::string> value;

  explicit operator bool() const noexcept { return value.has_value(); };
};

EnvironmentVarNameValue DF_GetEnvironmentVariable(std::string_view name);

u32 Crc32(std::string_view data);

// Converts a fixed-point decimal string (e.g. "2696.29") into an integer
// scaled by 10^decimals (e.g. 269629 for decimals=2), without a round trip
// through double. A fraction with fewer digits than `decimals` is
// zero-padded; a fraction with more is truncated.
i64 ScaleDecimalStringToI64(std::string_view value, u32 decimals);

}  // namespace data_feed

#endif  // ! DATA_FEED_UTILITY_H_
