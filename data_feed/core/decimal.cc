#include "core/decimal.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string_view>

#include "core/aliasing.h"

namespace core {

namespace {

i64 Pow10(u32 exponent) {
  i64 result{1};
  for (u32 i{0}; i < exponent; ++i) {
    result *= 10;
  }
  return result;
}

}  // namespace

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

u32 FractionDigits(std::string_view value) {
  const std::size_t dot = value.find('.');
  if (dot == std::string_view::npos) {
    return 0;
  }
  const std::size_t last_digit = value.find_last_not_of('0');
  return last_digit <= dot ? 0 : static_cast<u32>(last_digit - dot);
}

}  // namespace core
