#include "l2/book.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "l2/message.h"

namespace l2 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

Message MakeMessage(Message::Type type,
                    std::vector<Message::Level> bids,
                    std::vector<Message::Level> asks) {
  return Message{.type = type,
                 .checksum = 0,
                 .venue_time_ns = 0,
                 .capture_time_ns = 0,
                 .symbol = "BTC/USD",
                 .buys = std::move(bids),
                 .sells = std::move(asks)};
}

TEST(BookTest, KeepsEachSideBestFirst) {
  Book book{10};

  book.Apply(MakeMessage(
      Message::Type::kSnapshot,
      {{.price = 9, .quantity = 1}, {.price = 10, .quantity = 2}},
      {{.price = 12, .quantity = 3}, {.price = 11, .quantity = 4}}));

  EXPECT_THAT(book.bids(), ElementsAre(Pair(10, 2), Pair(9, 1)));
  EXPECT_THAT(book.asks(), ElementsAre(Pair(11, 4), Pair(12, 3)));
}

TEST(BookTest, SetsAndRemovesLevelsOnAnUpdate) {
  Book book{10};
  book.Apply(MakeMessage(
      Message::Type::kSnapshot,
      {{.price = 10, .quantity = 1}, {.price = 9, .quantity = 1}}, {}));

  book.Apply(MakeMessage(Message::Type::kUpdate,
                         {{.price = 10, .quantity = 5},
                          {.price = 9, .quantity = 0},
                          {.price = 8, .quantity = 2}},
                         {}));

  EXPECT_THAT(book.bids(), ElementsAre(Pair(10, 5), Pair(8, 2)));
}

TEST(BookTest, ReplacesEverythingOnASnapshot) {
  Book book{10};
  book.Apply(MakeMessage(Message::Type::kSnapshot,
                         {{.price = 10, .quantity = 1}},
                         {{.price = 11, .quantity = 1}}));

  book.Apply(MakeMessage(Message::Type::kSnapshot,
                         {{.price = 20, .quantity = 2}}, {}));

  EXPECT_THAT(book.bids(), ElementsAre(Pair(20, 2)));
  EXPECT_THAT(book.asks(), IsEmpty());
}

TEST(BookTest, DropsWhatFallsBeyondTheDepth) {
  Book book{2};
  book.Apply(MakeMessage(
      Message::Type::kSnapshot,
      {{.price = 10, .quantity = 1}, {.price = 9, .quantity = 1}}, {}));

  // A better bid pushes the worst out, and removing it again does not bring
  // that one back: the venue never resends it.
  book.Apply(
      MakeMessage(Message::Type::kUpdate, {{.price = 11, .quantity = 1}}, {}));
  book.Apply(
      MakeMessage(Message::Type::kUpdate, {{.price = 11, .quantity = 0}}, {}));

  EXPECT_THAT(book.bids(), ElementsAre(Pair(10, 1)));
}

TEST(BookTest, EmptiesOnClear) {
  Book book{10};
  book.Apply(MakeMessage(Message::Type::kSnapshot,
                         {{.price = 10, .quantity = 1}},
                         {{.price = 11, .quantity = 1}}));

  book.Clear();

  EXPECT_THAT(book.bids(), IsEmpty());
  EXPECT_THAT(book.asks(), IsEmpty());
}

}  // namespace
}  // namespace l2
