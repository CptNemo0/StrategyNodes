#include "utility.h"

#include <sec_api/stdlib_s.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "aliasing.h"

namespace data_feed {

namespace {

i64 Pow10(u32 exponent) {
  i64 result{1};
  for (u32 i{0}; i < exponent; ++i) {
    result *= 10;
  }
  return result;
}

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

i64 ScaleDecimalStringToI64(std::string_view value, u32 decimals) {
  const std::size_t dot = value.find('.');
  const std::string_view integer_part = value.substr(0, dot);
  const std::string_view fraction_part = dot == std::string_view::npos
                                             ? std::string_view{}
                                             : value.substr(dot + 1);

  i64 integer_value{0};
  std::from_chars(integer_part.data(),
                  integer_part.data() + integer_part.size(), integer_value);

  const std::size_t digits_used =
      std::min<std::size_t>(fraction_part.size(), decimals);
  i64 fraction_value{0};
  if (digits_used > 0) {
    std::from_chars(fraction_part.data(), fraction_part.data() + digits_used,
                    fraction_value);
  }

  return integer_value * Pow10(decimals) +
         fraction_value * Pow10(decimals - static_cast<u32>(digits_used));
}

}  // namespace data_feed
