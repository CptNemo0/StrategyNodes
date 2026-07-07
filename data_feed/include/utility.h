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

i64 double_string_to_i64(std::string_view value);

}  // namespace data_feed

#endif  // ! DATA_FEED_UTILITY_H_
