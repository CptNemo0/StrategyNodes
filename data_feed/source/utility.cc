#include "utility.h"

#include <sec_api/stdlib_s.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
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

// Reads exactly `count` decimal digits at `offset`, or nullopt if any of them
// is missing or not a digit.
std::optional<i64> ReadDigits(std::string_view text,
                              std::size_t offset,
                              std::size_t count) {
  if (offset + count > text.size()) {
    return std::nullopt;
  }

  i64 value{0};
  const std::from_chars_result result = std::from_chars(
      text.data() + offset, text.data() + offset + count, value);
  return result.ptr == text.data() + offset + count ? std::optional{value}
                                                    : std::nullopt;
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

i64 UnixNanosNow() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::optional<i64> ParseIso8601ToUnixNanos(std::string_view text) {
  // Offsets into the fixed "YYYY-MM-DDTHH:MM:SS" prefix.
  constexpr std::size_t kSecondsEnd = 19;
  if (text.size() < kSecondsEnd || text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' || text[16] != ':') {
    return std::nullopt;
  }

  const std::optional<i64> year = ReadDigits(text, 0, 4);
  const std::optional<i64> month = ReadDigits(text, 5, 2);
  const std::optional<i64> day = ReadDigits(text, 8, 2);
  const std::optional<i64> hour = ReadDigits(text, 11, 2);
  const std::optional<i64> minute = ReadDigits(text, 14, 2);
  const std::optional<i64> second = ReadDigits(text, 17, 2);
  if (!year || !month || !day || !hour || !minute || !second) {
    return std::nullopt;
  }

  // Fractional seconds are optional and of any length; each digit is worth a
  // tenth of the previous one, and anything past nanoseconds is dropped.
  i64 fraction_nanos{0};
  if (text.size() > kSecondsEnd && text[kSecondsEnd] == '.') {
    i64 digit_value{100'000'000};
    for (const char digit : text.substr(kSecondsEnd + 1)) {
      if (digit < '0' || digit > '9') {
        break;
      }
      fraction_nanos += (digit - '0') * digit_value;
      digit_value /= 10;
    }
  }

  const std::chrono::sys_days date{
      std::chrono::year{static_cast<int>(*year)} /
      std::chrono::month{static_cast<unsigned>(*month)} /
      std::chrono::day{static_cast<unsigned>(*day)}};

  constexpr i64 kNanosPerSecond{1'000'000'000};
  return ((date.time_since_epoch().count() * 86400) + (*hour * 3600) +
          (*minute * 60) + *second) *
             kNanosPerSecond +
         fraction_nanos;
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
