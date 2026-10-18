#include "venue/adapter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "venue/manager.h"

namespace venue {
namespace {

using ::testing::FieldsAre;
using ::testing::Optional;

// Letters, digits and the venue's own punctuation make a symbol.
TEST(IsWellFormedSymbolTest, AcceptsLettersDigitsAndTheGivenPunctuation) {
  EXPECT_TRUE(IsWellFormedSymbol("BTC/USD", "/"));
  EXPECT_TRUE(IsWellFormedSymbol("ETH2.S-USD", ".-"));
  EXPECT_TRUE(IsWellFormedSymbol(std::string(kMaxSymbolLength, 'A'), ""));
}

// Anything that could add to a request is refused: other punctuation, JSON
// or path syntax, nothing at all, or far too much.
TEST(IsWellFormedSymbolTest, RefusesAnythingElse) {
  EXPECT_FALSE(IsWellFormedSymbol("BTC/USD", "-"));
  EXPECT_FALSE(IsWellFormedSymbol(R"(BTC-USD"],"x":["y)", "-"));
  EXPECT_FALSE(IsWellFormedSymbol("../time", "-"));
  EXPECT_FALSE(IsWellFormedSymbol("", "-"));
  EXPECT_FALSE(IsWellFormedSymbol(std::string(kMaxSymbolLength + 1, 'A'), ""));
}

// A call that returns passes its result through.
TEST(CallVenueTest, ReturnsWhatTheCallReturns) {
  EXPECT_THAT(CallVenue([] { return 7; }), Optional(7));
}

// An UnknownSymbolError is a symbol the venue does not list; anything else
// is the venue failing. Both keep the exception's message.
TEST(CallVenueTest, TurnsExceptionsIntoErrors) {
  EXPECT_THAT(CallVenue([]() -> int {
                throw UnknownSymbolError{"no FOO/BAR"};
              }).error(),
              FieldsAre(Error::Code::kUnknownSymbol, "no FOO/BAR"));
  EXPECT_THAT(
      CallVenue([]() -> int { throw std::runtime_error{"timed out"}; }).error(),
      FieldsAre(Error::Code::kVenueFailed, "timed out"));
}

}  // namespace
}  // namespace venue
