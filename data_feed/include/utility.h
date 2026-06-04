#ifndef DATA_FEED_UTILITY_H_
#define DATA_FEED_UTILITY_H_

#include <optional>
#include <string>
#include <string_view>

namespace data_feed {

struct EnvironmentVarNameValue {
  std::string_view name;
  std::optional<std::string> value;

  explicit operator bool() const noexcept { return value.has_value(); };
};

EnvironmentVarNameValue DF_GetEnvironmentVariable(std::string_view name);

}  // namespace data_feed

#endif  // ! DATA_FEED_UTILITY_H_
