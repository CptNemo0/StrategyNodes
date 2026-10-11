#include "frontend/wire.h"

#include <gtest/gtest.h>

#include <optional>
#include <ranges>

#include "core/aliasing.h"
#include "l2/message.h"
#include "venue/manager.h"

namespace frontend {
namespace {

TEST(WireTest, MapsEveryVenueErrorCode) {
  const auto code = [](venue::Error::Code venue_code) {
    return MakeCommandError(venue::Error{.code = venue_code, .detail = "d"})
        .code();
  };

  EXPECT_EQ(code(venue::Error::Code::kUnknownVenue),
            wire::CommandError::CODE_INVALID_COMMAND);
  EXPECT_EQ(code(venue::Error::Code::kInvalidDepth),
            wire::CommandError::CODE_INVALID_DEPTH);
  EXPECT_EQ(code(venue::Error::Code::kUnknownSymbol),
            wire::CommandError::CODE_UNKNOWN_SYMBOL);
  EXPECT_EQ(code(venue::Error::Code::kUnknownId),
            wire::CommandError::CODE_UNKNOWN_ID);
  EXPECT_EQ(code(venue::Error::Code::kSinkMismatch),
            wire::CommandError::CODE_SINK_MISMATCH);
  EXPECT_EQ(code(venue::Error::Code::kSymbolInUse),
            wire::CommandError::CODE_SYMBOL_IN_USE);
  EXPECT_EQ(code(venue::Error::Code::kVenueFailed),
            wire::CommandError::CODE_VENUE_FAILED);
}

TEST(WireTest, KeepsTheErrorDetail) {
  EXPECT_EQ(
      MakeCommandError(venue::Error{.code = venue::Error::Code::kUnknownId,
                                    .detail = "No subscription 3"})
          .detail(),
      "No subscription 3");
}

TEST(WireTest, RoundTripsSinksAndVenues) {
  EXPECT_EQ(FromWire(ToWire(venue::Sink::kLive)), venue::Sink::kLive);
  EXPECT_EQ(FromWire(ToWire(venue::Sink::kRecord)), venue::Sink::kRecord);
  EXPECT_EQ(FromWire(ToWire(venue::Id::kKraken)), venue::Id::kKraken);
}

TEST(WireTest, RejectsUnspecifiedSinksAndVenues) {
  EXPECT_EQ(FromWire(wire::SINK_UNSPECIFIED), std::nullopt);
  EXPECT_EQ(FromWire(wire::VENUE_UNSPECIFIED), std::nullopt);
}

TEST(WireTest, MakesABookFromAMessage) {
  const wire::Book book =
      MakeBook(4, l2::Message{.type = l2::Message::Type::kUpdate,
                              .checksum = 1,
                              .venue_time_ns = 2,
                              .capture_time_ns = 3,
                              .symbol = "BTC/USD",
                              .buys = {{.price = 10, .quantity = 0}},
                              .sells = {{.price = 11, .quantity = 5},
                                        {.price = 12, .quantity = 6}}});

  EXPECT_EQ(book.stream_id(), 4u);
  EXPECT_EQ(book.kind(), wire::Book::KIND_DELTA);
  ASSERT_EQ(book.bids_size(), 1);
  EXPECT_EQ(book.bids(0).quantity(), 0);
  ASSERT_EQ(book.asks_size(), 2);
  EXPECT_EQ(book.asks(1).price(), 12);
  EXPECT_EQ(book.checksum(), 1u);
  EXPECT_EQ(book.venue_time_ns(), 2);
  EXPECT_EQ(book.capture_time_ns(), 3);
}

TEST(WireTest, FramesAPayloadAsTheEventCarryingIt) {
  wire::Book book;
  book.set_stream_id(3);
  // Enough levels that the payload's length takes more than one byte.
  for (const i64 price : std::views::iota(i64{1}, i64{100})) {
    wire::Level& level = *book.add_bids();
    level.set_price(price);
    level.set_quantity(price * 2);
  }

  wire::MarketDataEvent event;
  ASSERT_TRUE(event.ParseFromString(
      Frame(/*sequence=*/300, /*send_time_ns=*/-5, MakePayload(book))));

  EXPECT_EQ(event.sequence(), 300u);
  EXPECT_EQ(event.send_time_ns(), -5);
  ASSERT_TRUE(event.has_book());
  EXPECT_EQ(event.book().SerializeAsString(), book.SerializeAsString());
}

TEST(WireTest, FramesAnEmptyPayload) {
  wire::MarketDataEvent event;
  ASSERT_TRUE(
      event.ParseFromString(Frame(0, 0, MakePayload(wire::Heartbeat{}))));

  EXPECT_TRUE(event.has_heartbeat());
}

}  // namespace
}  // namespace frontend
