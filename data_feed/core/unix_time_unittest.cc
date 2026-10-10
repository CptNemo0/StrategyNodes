#include "core/unix_time.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string_view>

#include "core/aliasing.h"

namespace core {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Message;
using ::testing::Optional;

constexpr i64 kNanosPerSecond{1'000'000'000};

void ExpectParses(std::string_view text, i64 expected_nanos) {
  SCOPED_TRACE(Message() << '"' << text << '"');
  EXPECT_THAT(ParseIso8601ToUnixNanos(text), Optional(expected_nanos));
}

void ExpectRejects(std::string_view text) {
  SCOPED_TRACE(Message() << '"' << text << '"');
  EXPECT_THAT(ParseIso8601ToUnixNanos(text), Eq(std::nullopt));
}

// The Unix epoch itself parses to zero.
TEST(ParseIso8601ToUnixNanosTest, ParsesTheEpoch) {
  ExpectParses("1970-01-01T00:00:00Z", 0);
}

// A timestamp exactly as Kraken sends it parses to nanoseconds.
TEST(ParseIso8601ToUnixNanosTest, ParsesAKrakenTimestamp) {
  ExpectParses("2026-10-04T18:32:21.811584Z",
               1791138741 * kNanosPerSecond + 811'584'000);
}

// Calendar arithmetic counts leap days.
TEST(ParseIso8601ToUnixNanosTest, AccountsForLeapDays) {
  ExpectParses("2000-02-29T00:00:00Z", 951782400 * kNanosPerSecond);
}

// Fractional seconds of any length are scaled by their position.
TEST(ParseIso8601ToUnixNanosTest, ScalesFractionsOfAnyLength) {
  constexpr i64 kBase = 1790508579 * kNanosPerSecond;
  ExpectParses("2026-09-27T11:29:39.5Z", kBase + 500'000'000);
  ExpectParses("2026-09-27T11:29:39.799Z", kBase + 799'000'000);
  ExpectParses("2026-09-27T11:29:39.799080Z", kBase + 799'080'000);
  ExpectParses("2026-09-27T11:29:39.123456789Z", kBase + 123'456'789);
}

// Sub-nanosecond digits are dropped rather than rounded.
TEST(ParseIso8601ToUnixNanosTest, DropsDigitsPastNanosecondResolution) {
  ExpectParses("1970-01-01T00:00:00.123456789999Z", 123'456'789);
}

// Whole-second timestamps parse, with or without the trailing Z.
TEST(ParseIso8601ToUnixNanosTest, AcceptsATimestampWithoutAFraction) {
  ExpectParses("2026-09-27T11:29:39Z", 1790508579 * kNanosPerSecond);
  ExpectParses("2026-09-27T11:29:39", 1790508579 * kNanosPerSecond);
}

// Text not shaped like YYYY-MM-DDTHH:MM:SS is rejected.
TEST(ParseIso8601ToUnixNanosTest, RejectsTextNotInTheExpectedLayout) {
  ExpectRejects("");
  ExpectRejects("2026-09-27");
  ExpectRejects("2026-09-27 11:29:39Z");
  ExpectRejects("2026/09/27T11:29:39Z");
  ExpectRejects("2026-09-27T11-29-39Z");
  ExpectRejects("not a timestamp at all");
}

// A field holding anything but digits is rejected.
TEST(ParseIso8601ToUnixNanosTest, RejectsNonDigitFields) {
  ExpectRejects("20X6-09-27T11:29:39Z");
  ExpectRejects("2026-0A-27T11:29:39Z");
  ExpectRejects("2026-09-27T1 :29:39Z");
  ExpectRejects("2026-09-27T11:29:3xZ");
}

// Timestamps across a second boundary keep their order and distance.
TEST(ParseIso8601ToUnixNanosTest, PreservesOrderBetweenTimestamps) {
  const std::optional<i64> earlier =
      ParseIso8601ToUnixNanos("2026-09-27T11:29:39.999999Z");

  ASSERT_TRUE(earlier.has_value());
  EXPECT_THAT(ParseIso8601ToUnixNanos("2026-09-27T11:29:40.000001Z"),
              Optional(*earlier + 2'000));
}

i64 SystemClockNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// UnixNanosNow() reads the system clock, in nanoseconds since the epoch.
TEST(UnixNanosNowTest, AgreesWithTheSystemClock) {
  const i64 before = SystemClockNanos();
  const i64 now = UnixNanosNow();
  const i64 after = SystemClockNanos();

  EXPECT_THAT(now, AllOf(Ge(before), Le(after)));
}

}  // namespace
}  // namespace core
