#include "kraken/checksum.h"

#include <gtest/gtest.h>

#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/crc32.h"
#include "l2/book.h"
#include "l2/message.h"

namespace kraken {
namespace {

// The worked example of Kraken's book guide
// (https://docs.kraken.com/api/docs/guides/spot-ws-book-v2): a BTC/USD book
// at price precision 1 and quantity precision 8, as fixed-point integers.
const std::vector<l2::Message::Level> kDocsAsks = {
    {.price = 452852, .quantity = 100000},
    {.price = 452864, .quantity = 154571953},
    {.price = 452866, .quantity = 154571109},
    {.price = 452896, .quantity = 154560911},
    {.price = 452902, .quantity = 15890660},
    {.price = 452918, .quantity = 154553491},
    {.price = 452947, .quantity = 4454749},
    {.price = 452961, .quantity = 35380000},
    {.price = 452975, .quantity = 9945542},
    {.price = 452995, .quantity = 18772827},
};
const std::vector<l2::Message::Level> kDocsBids = {
    {.price = 452835, .quantity = 10000000},
    {.price = 452834, .quantity = 154582015},
    {.price = 452821, .quantity = 10000000},
    {.price = 452810, .quantity = 10000000},
    {.price = 452803, .quantity = 154592586},
    {.price = 452790, .quantity = 7990000},
    {.price = 452776, .quantity = 3310103},
    {.price = 452775, .quantity = 30000000},
    {.price = 452773, .quantity = 154602737},
    {.price = 452766, .quantity = 15445238},
};
// What the guide gives as the checksum's input, and the checksum.
constexpr std::string_view kDocsChecksumInput =
    "452852100000452864154571953452866154571109452896154560911452902158906604"
    "529181545534914529474454749452961353800004529759945542452995187728274528"
    "351000000045283415458201545282110000000452810100000004528031545925864527"
    "90799000045277633101034527753000000045277315460273745276615445238";
constexpr u32 kDocsChecksum = 3310070434;

l2::Book MakeBook(u32 depth,
                  std::vector<l2::Message::Level> bids,
                  std::vector<l2::Message::Level> asks) {
  l2::Book book{depth};
  book.Apply(l2::Message{.type = l2::Message::Type::kSnapshot,
                         .checksum = 0,
                         .venue_time_ns = 0,
                         .capture_time_ns = 0,
                         .symbol = "BTC/USD",
                         .buys = std::move(bids),
                         .sells = std::move(asks)});
  return book;
}

TEST(ChecksumTest, UsesTheCrc32KrakenUses) {
  EXPECT_EQ(core::Crc32(kDocsChecksumInput), kDocsChecksum);
}

TEST(ChecksumTest, MatchesKrakensWorkedExample) {
  EXPECT_EQ(BookChecksum(MakeBook(10, kDocsBids, kDocsAsks)), kDocsChecksum);
}

TEST(ChecksumTest, OrdersLevelsBestFirstWhateverOrderTheyCameIn) {
  EXPECT_EQ(
      BookChecksum(MakeBook(
          10, kDocsBids | std::views::reverse | std::ranges::to<std::vector>(),
          kDocsAsks | std::views::reverse | std::ranges::to<std::vector>())),
      kDocsChecksum);
}

TEST(ChecksumTest, CoversOnlyTheTopTenLevels) {
  std::vector<l2::Message::Level> asks = kDocsAsks;
  asks.push_back({.price = 453000, .quantity = 1});
  std::vector<l2::Message::Level> bids = kDocsBids;
  bids.push_back({.price = 452000, .quantity = 1});

  EXPECT_EQ(BookChecksum(MakeBook(25, bids, asks)), kDocsChecksum);
}

TEST(ChecksumTest, ChangesWithAnyLevelInTheTopTen) {
  std::vector<l2::Message::Level> bids = kDocsBids;
  bids[9].quantity += 1;

  EXPECT_NE(BookChecksum(MakeBook(10, bids, kDocsAsks)), kDocsChecksum);
}

}  // namespace
}  // namespace kraken
