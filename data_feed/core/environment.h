#ifndef DATA_FEED_CORE_ENVIRONMENT_H_
#define DATA_FEED_CORE_ENVIRONMENT_H_

#include <optional>
#include <string>
#include <string_view>

namespace core {

struct EnvironmentVarNameValue {
  std::string_view name;
  std::optional<std::string> value;

  explicit operator bool() const noexcept { return value.has_value(); };
};

// Not GetEnvironmentVariable: <windows.h> macro-expands that name to
// GetEnvironmentVariableA/W.
EnvironmentVarNameValue ReadEnvironmentVariable(std::string_view name);

}  // namespace core

#endif  // ! DATA_FEED_CORE_ENVIRONMENT_H_
