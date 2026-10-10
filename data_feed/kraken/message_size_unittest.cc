#include "kraken/message_size.h"

#include <gtest/gtest.h>

#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/aliasing.h"

namespace kraken {
namespace {

using ::testing::Message;

// A "book" snapshot as Kraken sends it, with `depth` levels a side, each
// price and quantity rendered as `number`.
std::string BuildSnapshot(std::string_view symbol,
                          u64 depth,
                          std::string_view number,
                          std::string_view checksum) {
  const std::string side =
      std::views::repeat(
          std::format(R"({{"price":{},"qty":{}}})", number, number), depth) |
      std::views::join_with(',') | std::ranges::to<std::string>();
  return std::format(
      R"({{"channel":"book","type":"snapshot","data":[{{"symbol":"{}",)"
      R"("bids":[{}],"asks":[{}],"checksum":{},)"
      R"("timestamp":"2026-09-27T11:29:39.799080Z"}}]}})",
      symbol, side, side, checksum);
}

// Depths Kraken's book channel does not offer are rejected.
TEST(MaxL2SnapshotLengthTest, RejectsDepthsKrakenDoesNotOffer) {
  for (const u64 depth : {0, 1, 11, 50, 1001}) {
    SCOPED_TRACE(Message() << "depth " << depth);
    EXPECT_THROW(MaxL2SnapshotLength(depth, 7), std::invalid_argument);
  }
}

// A deeper book allows a longer snapshot.
TEST(MaxL2SnapshotLengthTest, GrowsWithDepth) {
  for (const auto [shallower, deeper] : kL2Depths | std::views::pairwise) {
    SCOPED_TRACE(Message() << shallower << " vs " << deeper);
    EXPECT_LT(MaxL2SnapshotLength(shallower, 7),
              MaxL2SnapshotLength(deeper, 7));
  }
}

// The symbol's length is added to the bound as is.
TEST(MaxL2SnapshotLengthTest, GrowsByExactlyTheSymbolLength) {
  for (const u64 depth : kL2Depths) {
    SCOPED_TRACE(Message() << "depth " << depth);
    EXPECT_EQ(MaxL2SnapshotLength(depth, 12) - MaxL2SnapshotLength(depth, 7),
              5u);
  }
}

// A realistic snapshot at every depth fits within the bound.
TEST(MaxL2SnapshotLengthTest, BoundsATypicalSnapshot) {
  for (const u64 depth : kL2Depths) {
    SCOPED_TRACE(Message() << "depth " << depth);
    EXPECT_LE(BuildSnapshot("BTC/USD", depth, "84888.7", "1162203035").size(),
              MaxL2SnapshotLength(depth, 7));
  }
}

// Even a snapshot made of the longest possible numbers fits within the
// bound.
TEST(MaxL2SnapshotLengthTest, BoundsTheLongestPossibleSnapshot) {
  // The longest shortest-round-trip double and the largest u32.
  constexpr std::string_view kLongestNumber = "-1.2345678901234567e-308";
  constexpr std::string_view kLongestChecksum = "4294967295";
  constexpr std::string_view kSymbol = "LONGASSET/OTHERASSET";

  for (const u64 depth : kL2Depths) {
    SCOPED_TRACE(Message() << "depth " << depth);
    EXPECT_LE(
        BuildSnapshot(kSymbol, depth, kLongestNumber, kLongestChecksum).size(),
        MaxL2SnapshotLength(depth, kSymbol.size()));
  }
}

}  // namespace
}  // namespace kraken
