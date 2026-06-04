#include "utility.h"

#include <sec_api/stdlib_s.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace data_feed {

EnvironmentVarNameValue DF_GetEnvironmentVariable(std::string_view name) {
#ifdef _WIN32
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name.data()) != 0 || value == nullptr) {
    return {name, std::nullopt};
  }
  std::string result(value);
  std::free(value);
  return {name, std::move(result)};
#else
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

}  // namespace data_feed
