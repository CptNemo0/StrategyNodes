#include "core/unix_time.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>

#include "core/aliasing.h"

namespace core {

namespace {

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

}  // namespace

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

}  // namespace core
