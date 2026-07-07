#include "utility.h"

#include <sec_api/stdlib_s.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "aliasing.h"

namespace data_feed {

namespace {

constexpr std::array<u32, 256> BuildCrc32Table() {
  std::array<u32, 256> table{};
  for (u32 value{}; value < table.size(); ++value) {
    u32 crc{value};
    for (u32 bit{}; bit < 8; ++bit) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    table[value] = crc;
  }
  return table;
}

}  // namespace

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

u32 Crc32(std::string_view data) {
  static constexpr std::array<u32, 256> kTable = BuildCrc32Table();

  u32 crc = 0xFFFFFFFFu;
  for (const unsigned char byte : data) {
    crc = kTable[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

i64 double_string_to_i64(std::string_view value) {
  i64 return_value{0};
  for (char digit : value | std::ranges::views::filter(
                                [](char c) { return c >= '0' && c <= '9'; })) {
    return_value += digit - '0';
    return_value *= 10;
  }
  return return_value / 10;
}

}  // namespace data_feed
