#include "core/decimal.h"

#include <gtest/gtest.h>

#include <string_view>

#include "core/aliasing.h"

namespace core {
namespace {

using ::testing::Message;

struct ScaleCase {
  std::string_view text;
  u32 decimals;
  i64 expected;
};

void ExpectScales(const ScaleCase& scale_case) {
  SCOPED_TRACE(Message() << '"' << scale_case.text << "\" at "
                         << scale_case.decimals << " decimals");
  EXPECT_EQ(ScaleDecimalStringToI64(scale_case.text, scale_case.decimals),
            scale_case.expected);
}

// Decimal text becomes an exact scaled integer, with no floating-point
// rounding along the way.
TEST(ScaleDecimalStringToI64Test, ScalesAnExactFractionWithoutLoss) {
  ExpectScales({.text = "2696.29", .decimals = 2, .expected = 269629});
  ExpectScales({.text = "0.06980997", .decimals = 8, .expected = 6980997});
  // 0.1 has no exact double; going through one would risk 9999999.
  ExpectScales({.text = "0.1", .decimals = 8, .expected = 10000000});
}

// A fraction with fewer digits than the precision is padded with zeros.
TEST(ScaleDecimalStringToI64Test, ZeroPadsAShortFraction) {
  ExpectScales({.text = "84888.7", .decimals = 2, .expected = 8488870});
  ExpectScales({.text = "1.5", .decimals = 4, .expected = 15000});
}

// Digits past the precision are truncated, never rounded up.
TEST(ScaleDecimalStringToI64Test, TruncatesAFractionLongerThanThePrecision) {
  ExpectScales({.text = "1.239", .decimals = 2, .expected = 123});
  ExpectScales({.text = "0.999999", .decimals = 1, .expected = 9});
}

// Whole numbers, with or without a trailing dot, scale by the full power
// of ten.
TEST(ScaleDecimalStringToI64Test, ScalesAnIntegerWithoutAFraction) {
  ExpectScales({.text = "42", .decimals = 3, .expected = 42000});
  ExpectScales({.text = "42.", .decimals = 3, .expected = 42000});
}

// At zero decimals only the integer part counts.
TEST(ScaleDecimalStringToI64Test, DropsTheFractionAtZeroDecimals) {
  ExpectScales({.text = "42.987", .decimals = 0, .expected = 42});
  ExpectScales({.text = "7", .decimals = 0, .expected = 7});
}

// Leading zeros of the fraction keep their place value.
TEST(ScaleDecimalStringToI64Test, KeepsLeadingZerosOfTheFraction) {
  ExpectScales({.text = "0.005", .decimals = 3, .expected = 5});
  ExpectScales({.text = "3.05", .decimals = 2, .expected = 305});
}

// Zero scales to zero at any precision.
TEST(ScaleDecimalStringToI64Test, ScalesZero) {
  ExpectScales({.text = "0", .decimals = 8, .expected = 0});
  ExpectScales({.text = "0.00000000", .decimals = 8, .expected = 0});
}

// Large values at high precision still fit and stay exact.
TEST(ScaleDecimalStringToI64Test, ScalesLargePricesAtHighPrecision) {
  ExpectScales({.text = "123456789.12345678",
                .decimals = 8,
                .expected = 12345678912345678});
}

// A step size's fraction digits ignore trailing zeros, which venues spell
// differently ("0.01" and "0.01000000" are the same tick).
TEST(FractionDigitsTest, CountsTheFractionDigitsUpToTheLastNonZeroOne) {
  EXPECT_EQ(FractionDigits("0.01"), 2u);
  EXPECT_EQ(FractionDigits("0.01000000"), 2u);
  EXPECT_EQ(FractionDigits("0.00000001"), 8u);
  EXPECT_EQ(FractionDigits("0.5"), 1u);
  EXPECT_EQ(FractionDigits("22356.270000"), 2u);
}

// Whole numbers need no fraction digits, however they are spelled.
TEST(FractionDigitsTest, IsZeroForAWholeNumber) {
  EXPECT_EQ(FractionDigits("1"), 0u);
  EXPECT_EQ(FractionDigits("10"), 0u);
  EXPECT_EQ(FractionDigits("100."), 0u);
  EXPECT_EQ(FractionDigits("100.000"), 0u);
  EXPECT_EQ(FractionDigits("0"), 0u);
}

}  // namespace
}  // namespace core
