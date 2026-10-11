#include "frontend/transmitter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/outbox.h"
#include "frontend/watcher.h"
#include "frontend/wire.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace frontend {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Test;

constexpr l2::PairPrecision kPrecision{.price_decimals = 1, .qty_decimals = 8};
constexpr venue::SubscriptionId kId = 7;
constexpr u32 kDepth = 3;

// Keeps every payload it is sent, and the event each one frames.
class RecordingWatcher : public Watcher {
 public:
  void Send(Payload payload) override {
    wire::MarketDataEvent event;
    EXPECT_TRUE(event.ParseFromString(Frame(0, 0, payload)));
    const std::scoped_lock lock{mutex_};
    payloads_.push_back(std::move(payload));
    events_.push_back(std::move(event));
  }

  std::vector<wire::MarketDataEvent> events() const {
    const std::scoped_lock lock{mutex_};
    return events_;
  }

  std::vector<Payload> payloads() const {
    const std::scoped_lock lock{mutex_};
    return payloads_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Payload> payloads_;
  std::vector<wire::MarketDataEvent> events_;
};

l2::Message MakeMessage(l2::Message::Type type,
                        std::vector<l2::Message::Level> buys,
                        std::vector<l2::Message::Level> sells,
                        u32 checksum = 0) {
  return l2::Message{.type = type,
                     .checksum = checksum,
                     .venue_time_ns = 100 + checksum,
                     .capture_time_ns = 200 + checksum,
                     .symbol = "BTC/USD",
                     .buys = std::move(buys),
                     .sells = std::move(sells)};
}

// A level as {price, quantity}, for matching.
using Pair = std::pair<i64, i64>;

// A side of a wire Book as Pairs.
std::vector<Pair> Levels(
    const google::protobuf::RepeatedPtrField<wire::Level>& levels) {
  std::vector<Pair> pairs;
  for (const wire::Level& level : levels) {
    pairs.emplace_back(level.price(), level.quantity());
  }
  return pairs;
}

class TransmitterTest : public Test {
 protected:
  Transmitter& transmitter() {
    if (!transmitter_) {
      transmitter_.emplace(outbox_, kId, "kraken", "BTC/USD", kDepth,
                           kPrecision);
    }
    return *transmitter_;
  }

  // Watches subscription kId through the Outbox, as the Server does.
  void Watch(const std::shared_ptr<RecordingWatcher>& watcher) {
    transmitter();
    ASSERT_TRUE(outbox_.Watch(kId, watcher));
  }

  void Watch() { Watch(watcher_); }

  // Declared first, so it outlives the transmitter.
  Outbox outbox_;
  std::shared_ptr<RecordingWatcher> watcher_ =
      std::make_shared<RecordingWatcher>();
  std::optional<Transmitter> transmitter_;
};

TEST_F(TransmitterTest, RegistersWithTheOutboxWhileItExists) {
  transmitter();
  EXPECT_TRUE(outbox_.Watch(kId, watcher_));

  transmitter_.reset();

  EXPECT_FALSE(outbox_.Watch(kId, watcher_));
}

TEST_F(TransmitterTest, RefusesAnIdAlreadyRegistered) {
  transmitter();

  EXPECT_THROW(
      Transmitter(outbox_, kId, "kraken", "ETH/USD", kDepth, kPrecision),
      std::logic_error);
  // The first one is still the one registered.
  EXPECT_TRUE(outbox_.Watch(kId, watcher_));
}

TEST_F(TransmitterTest, SendsOnlyAStreamInfoBeforeTheFirstSnapshot) {
  Watch();

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(1));
  ASSERT_TRUE(events[0].has_stream_info());
  const wire::StreamInfo& info = events[0].stream_info();
  EXPECT_EQ(info.stream_id(), kId);
  EXPECT_EQ(info.symbol(), "BTC/USD");
  EXPECT_EQ(info.venue(), "kraken");
  EXPECT_EQ(info.depth(), kDepth);
  EXPECT_EQ(info.price_decimals(), 1u);
  EXPECT_EQ(info.qty_decimals(), 8u);
}

TEST_F(TransmitterTest, ForwardsEveryMessageToAWatcher) {
  Watch();

  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}},
                                    {{.price = 11, .quantity = 2}}, 5));
  transmitter().Receive(MakeMessage(l2::Message::Type::kUpdate,
                                    {{.price = 10, .quantity = 0}}, {}, 6));

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(3));
  const wire::Book& snapshot = events[1].book();
  EXPECT_EQ(snapshot.stream_id(), kId);
  EXPECT_EQ(snapshot.kind(), wire::Book::KIND_SNAPSHOT);
  EXPECT_THAT(Levels(snapshot.bids()), ElementsAre(Pair{10, 1}));
  EXPECT_THAT(Levels(snapshot.asks()), ElementsAre(Pair{11, 2}));
  EXPECT_EQ(snapshot.checksum(), 5u);
  EXPECT_EQ(snapshot.venue_time_ns(), 105);
  EXPECT_EQ(snapshot.capture_time_ns(), 205);
  const wire::Book& update = events[2].book();
  EXPECT_EQ(update.kind(), wire::Book::KIND_DELTA);
  // As the venue sent it, removal included.
  EXPECT_THAT(Levels(update.bids()), ElementsAre(Pair{10, 0}));
  EXPECT_THAT(update.asks(), IsEmpty());
}

TEST_F(TransmitterTest, GivesALateWatcherASnapshotOfTheBookSoFar) {
  transmitter().Receive(
      MakeMessage(l2::Message::Type::kSnapshot,
                  {{.price = 10, .quantity = 1}, {.price = 9, .quantity = 1}},
                  {{.price = 11, .quantity = 2}}, 1));
  transmitter().Receive(MakeMessage(
      l2::Message::Type::kUpdate,
      {{.price = 9, .quantity = 0}, {.price = 8, .quantity = 4}},
      {{.price = 11, .quantity = 3}, {.price = 12, .quantity = 5}}, 2));

  Watch();

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(2));
  EXPECT_TRUE(events[0].has_stream_info());
  const wire::Book& snapshot = events[1].book();
  EXPECT_EQ(snapshot.kind(), wire::Book::KIND_SNAPSHOT);
  // Best first: bids descending, asks ascending.
  EXPECT_THAT(Levels(snapshot.bids()), ElementsAre(Pair{10, 1}, Pair{8, 4}));
  EXPECT_THAT(Levels(snapshot.asks()), ElementsAre(Pair{11, 3}, Pair{12, 5}));
  // The last message's, which the book matches.
  EXPECT_EQ(snapshot.checksum(), 2u);
  EXPECT_EQ(snapshot.capture_time_ns(), 202);
}

TEST_F(TransmitterTest, TruncatesTheBookToItsDepth) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1},
                                     {.price = 9, .quantity = 1},
                                     {.price = 8, .quantity = 1}},
                                    {}));
  // A better bid pushes the worst one out of the top kDepth.
  transmitter().Receive(MakeMessage(l2::Message::Type::kUpdate,
                                    {{.price = 11, .quantity = 1}}, {}));
  // Removing a level does not bring the pushed out one back.
  transmitter().Receive(MakeMessage(l2::Message::Type::kUpdate,
                                    {{.price = 11, .quantity = 0}}, {}));

  Watch();

  EXPECT_THAT(Levels(watcher_->events()[1].book().bids()),
              ElementsAre(Pair{10, 1}, Pair{9, 1}));
}

TEST_F(TransmitterTest, LetsAVenueSnapshotReplaceTheBook) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}},
                                    {{.price = 11, .quantity = 1}}));
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 20, .quantity = 2}}, {}));

  Watch();

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(2));
  const wire::Book& snapshot = events[1].book();
  EXPECT_THAT(Levels(snapshot.bids()), ElementsAre(Pair{20, 2}));
  EXPECT_THAT(snapshot.asks(), IsEmpty());
}

TEST_F(TransmitterTest, SendsNothingOnceUnwatched) {
  Watch();
  outbox_.Unwatch(kId, *watcher_);

  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot, {}, {}));

  EXPECT_THAT(watcher_->events(), SizeIs(1));
}

TEST_F(TransmitterTest, StartsOverWhenWatchedAgain) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot, {}, {}));
  Watch();
  Watch();

  transmitter().Receive(MakeMessage(l2::Message::Type::kUpdate, {}, {}));

  // Two StreamInfos and snapshots, and the update only once.
  EXPECT_THAT(watcher_->events(), SizeIs(5));
}

TEST_F(TransmitterTest, SendsToEveryWatcher) {
  const std::shared_ptr<RecordingWatcher> other =
      std::make_shared<RecordingWatcher>();
  Watch();
  Watch(other);

  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot, {}, {}));

  EXPECT_THAT(watcher_->events(), SizeIs(2));
  EXPECT_THAT(other->events(), SizeIs(2));
}

TEST_F(TransmitterTest, SerializesEachMessageOnceForEveryWatcher) {
  const std::shared_ptr<RecordingWatcher> other =
      std::make_shared<RecordingWatcher>();
  Watch();
  Watch(other);

  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}}, {}));

  // The same bytes, not two copies of them.
  EXPECT_EQ(watcher_->payloads().back().bytes, other->payloads().back().bytes);
}

TEST_F(TransmitterTest, TellsWatchersWhenTheConnectionEnds) {
  Watch();
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}}, {}));

  transmitter().OnGap(core::Gap::kDisconnected);

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(3));
  ASSERT_TRUE(events[2].has_feed_status());
  EXPECT_EQ(events[2].feed_status().state(),
            wire::FeedStatus::STATE_DISCONNECTED);
  EXPECT_THAT(events[2].feed_status().healthy_stream_ids(), IsEmpty());
}

TEST_F(TransmitterTest, SaysTheStreamIsDegradedWhileResubscribing) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}}, {}));
  Watch();

  transmitter().OnGap(core::Gap::kResubscribing);
  const std::shared_ptr<RecordingWatcher> late =
      std::make_shared<RecordingWatcher>();
  Watch(late);

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(3));
  EXPECT_EQ(events[2].feed_status().state(), wire::FeedStatus::STATE_DEGRADED);
  // A late watcher gets the same status in place of the dropped book.
  ASSERT_THAT(late->events(), SizeIs(2));
  EXPECT_EQ(late->events()[1].feed_status().state(),
            wire::FeedStatus::STATE_DEGRADED);
}

TEST_F(TransmitterTest, GivesNoStaleSnapshotWhileDisconnected) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}}, {}));
  transmitter().OnGap(core::Gap::kDisconnected);

  Watch();

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(2));
  EXPECT_TRUE(events[0].has_stream_info());
  ASSERT_TRUE(events[1].has_feed_status());
  EXPECT_EQ(events[1].feed_status().state(),
            wire::FeedStatus::STATE_DISCONNECTED);
}

TEST_F(TransmitterTest, SaysTheStreamIsLiveAgainBeforeTheNewSnapshot) {
  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 10, .quantity = 1}}, {}));
  transmitter().OnGap(core::Gap::kDisconnected);
  Watch();

  transmitter().Receive(MakeMessage(l2::Message::Type::kSnapshot,
                                    {{.price = 20, .quantity = 2}}, {}));

  const std::vector<wire::MarketDataEvent> events = watcher_->events();
  ASSERT_THAT(events, SizeIs(4));
  ASSERT_TRUE(events[2].has_feed_status());
  EXPECT_EQ(events[2].feed_status().state(), wire::FeedStatus::STATE_LIVE);
  EXPECT_THAT(events[2].feed_status().healthy_stream_ids(), ElementsAre(kId));
  EXPECT_THAT(Levels(events[3].book().bids()), ElementsAre(Pair{20, 2}));
  // Later watchers get the new book, with no status.
  const std::shared_ptr<RecordingWatcher> late =
      std::make_shared<RecordingWatcher>();
  Watch(late);
  ASSERT_THAT(late->events(), SizeIs(2));
  EXPECT_THAT(Levels(late->events()[1].book().bids()),
              ElementsAre(Pair{20, 2}));
}

TEST(OutboxTest, IgnoresAnUnwatchOfAnUnknownId) {
  Outbox outbox;
  const RecordingWatcher watcher;

  outbox.Unwatch(kId, watcher);
}

}  // namespace
}  // namespace frontend
