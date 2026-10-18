#include "venue/router.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "core/unix_time.h"
#include "frontend/outbox.h"
#include "frontend/transmitter.h"
#include "l2/book_spec.h"
#include "l2/file_writer.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace venue {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::SizeIs;
using ::testing::Test;

constexpr l2::PairPrecision kPrecision{.price_decimals = 1, .qty_decimals = 8};
constexpr std::chrono::seconds kWaitLimit{5};
// Short enough that tests needing a reconnect do not wait for the default.
constexpr Backoff kFastBackoff{.initial = std::chrono::milliseconds{1},
                               .max = std::chrono::milliseconds{1}};
// The ids the Router hands out first and second.
constexpr SubscriptionId kFirstRecord =
    MakeSubscriptionId(1, Id::kKraken, Sink::kRecord);
constexpr SubscriptionId kFirstLive =
    MakeSubscriptionId(1, Id::kKraken, Sink::kLive);
constexpr SubscriptionId kSecondLive =
    MakeSubscriptionId(2, Id::kKraken, Sink::kLive);

// A venue that does what each test tells it and records what it was asked.
// The Router's supervisor thread calls it too, so every member is guarded.
class FakeManager : public Manager {
 public:
  std::string_view name() const override { return "fake"; }

  void Connect() override {
    const std::scoped_lock lock{mutex_};
    connect_attempts_.push_back(std::chrono::steady_clock::now());
    if (failing_connects_ > 0) {
      --failing_connects_;
      throw std::runtime_error{"venue unreachable"};
    }
    healthy_ = true;
  }

  bool healthy() const override {
    const std::scoped_lock lock{mutex_};
    return healthy_;
  }

  std::expected<SubscriptionId, Error> Subscribe(
      SubscriptionId id,
      l2::BookSpec book,
      ReceiverFactoryCallback make_receiver) override {
    const std::scoped_lock lock{mutex_};
    if (std::optional<std::expected<SubscriptionId, Error>> result =
            std::exchange(next_subscribe_result_, std::nullopt)) {
      return *std::move(result);
    }
    subscriptions_.push_back(SubscriptionInfo{
        .id = id, .book = std::move(book), .start_time_ns = 0});
    factories_.push_back(std::move(make_receiver));
    return id;
  }

  std::expected<void, Error> Unsubscribe(SubscriptionId id) override {
    const std::scoped_lock lock{mutex_};
    const auto subscription =
        std::ranges::find(subscriptions_, id, &SubscriptionInfo::id);
    if (subscription == subscriptions_.end()) {
      return std::unexpected{
          Error{.code = Error::Code::kUnknownId, .detail = "not here"}};
    }
    subscriptions_.erase(subscription);
    unsubscribed_.push_back(id);
    return {};
  }

  std::vector<SubscriptionInfo> List() const override {
    const std::scoped_lock lock{mutex_};
    return subscriptions_;
  }

  std::expected<PairList, Error> ListPairs() override {
    const std::scoped_lock lock{mutex_};
    return pairs_;
  }

  // What ListPairs() returns from then on.
  void AnswerListPairs(std::expected<PairList, Error> pairs) {
    const std::scoped_lock lock{mutex_};
    pairs_ = std::move(pairs);
  }

  // The next `count` Connect() calls throw.
  void FailConnects(int count) {
    const std::scoped_lock lock{mutex_};
    failing_connects_ = count;
  }

  // The next Subscribe() returns `result` without subscribing.
  void AnswerNextSubscribe(std::expected<SubscriptionId, Error> result) {
    const std::scoped_lock lock{mutex_};
    next_subscribe_result_ = std::move(result);
  }

  std::vector<std::chrono::steady_clock::time_point> connect_attempts() const {
    const std::scoped_lock lock{mutex_};
    return connect_attempts_;
  }

  std::vector<SubscriptionId> unsubscribed() const {
    const std::scoped_lock lock{mutex_};
    return unsubscribed_;
  }

  // Builds the receiver of the most recent subscription.
  std::unique_ptr<core::Receiver<l2::Message>> BuildLastReceiver() {
    const std::scoped_lock lock{mutex_};
    return factories_.back()(kPrecision);
  }

 private:
  mutable std::mutex mutex_;
  bool healthy_{false};
  int failing_connects_{0};
  std::optional<std::expected<SubscriptionId, Error>> next_subscribe_result_;
  std::vector<std::chrono::steady_clock::time_point> connect_attempts_;
  std::vector<SubscriptionInfo> subscriptions_;
  std::vector<ReceiverFactoryCallback> factories_;
  std::vector<SubscriptionId> unsubscribed_;
  std::expected<PairList, Error> pairs_;
};

// Polls `condition` until it holds or kWaitLimit passes.
bool WaitUntil(const std::function<bool()>& condition) {
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + kWaitLimit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return true;
}

class RouterTest : public Test {
 protected:
  // Hands the fake to a new Router, which owns it from then on.
  void Start(Backoff backoff = kFastBackoff) {
    Router::Managers managers;
    managers.emplace(Id::kKraken, std::move(owned_fake_));
    router_.emplace(std::move(managers), outbox_, backoff);
  }

  Router& router() { return *router_; }

  // Declared in this order, so the router, which owns the fake, goes first,
  // and the outbox, which its live subscriptions register with, last.
  frontend::Outbox outbox_;
  std::unique_ptr<FakeManager> owned_fake_ = std::make_unique<FakeManager>();
  FakeManager& fake_ = *owned_fake_;
  std::optional<Router> router_;
};

TEST_F(RouterTest, HandsOutIncreasingIds) {
  Start();

  EXPECT_EQ(router().Subscribe<Sink::kRecord>(
                Id::kKraken, {.symbol = "BTC/USD", .depth = 100}),
            kFirstRecord);
  EXPECT_EQ(router().Subscribe<Sink::kLive>(Id::kKraken,
                                            {.symbol = "ETH/USD", .depth = 10}),
            kSecondLive);
}

TEST_F(RouterTest, EncodesTheVenueAndSinkInTheId) {
  Start();

  const std::expected<SubscriptionId, Error> subscribed =
      router().Subscribe<Sink::kRecord>(Id::kKraken,
                                        {.symbol = "BTC/USD", .depth = 100});

  ASSERT_TRUE(subscribed);
  EXPECT_EQ(CounterOf(*subscribed), 1u);
  EXPECT_EQ(VenueOf(*subscribed), Id::kKraken);
  EXPECT_EQ(SinkOf(*subscribed), Sink::kRecord);
}

TEST_F(RouterTest, ListsSubscriptionsWithTheirVenue) {
  Start();
  ASSERT_TRUE(router().Subscribe<Sink::kRecord>(
      Id::kKraken, {.symbol = "BTC/USD", .depth = 100}));
  ASSERT_TRUE(router().Subscribe<Sink::kLive>(
      Id::kKraken, {.symbol = "ETH/USD", .depth = 10}));

  EXPECT_THAT(
      router().List(),
      ElementsAre(
          FieldsAre(Id::kKraken,
                    FieldsAre(kFirstRecord, FieldsAre("BTC/USD", 100u), 0)),
          FieldsAre(Id::kKraken,
                    FieldsAre(kSecondLive, FieldsAre("ETH/USD", 10u), 0))));
}

TEST_F(RouterTest, RefusesAVenueItHasNoManagerFor) {
  router_.emplace(Router::Managers{}, outbox_, kFastBackoff);

  const std::expected<SubscriptionId, Error> subscribed =
      router().Subscribe<Sink::kLive>(Id::kKraken,
                                      {.symbol = "BTC/USD", .depth = 100});

  ASSERT_FALSE(subscribed);
  EXPECT_EQ(subscribed.error().code, Error::Code::kUnknownVenue);
}

TEST_F(RouterTest, PassesOnTheVenuesError) {
  fake_.AnswerNextSubscribe(std::unexpected{
      Error{.code = Error::Code::kInvalidDepth, .detail = "no depth 7"}});
  Start();

  const std::expected<SubscriptionId, Error> subscribed =
      router().Subscribe<Sink::kLive>(Id::kKraken,
                                      {.symbol = "BTC/USD", .depth = 7});

  ASSERT_FALSE(subscribed);
  EXPECT_THAT(subscribed.error(),
              FieldsAre(Error::Code::kInvalidDepth, "no depth 7"));
  // The id it would have had names nothing.
  EXPECT_EQ(router().Unsubscribe(kFirstLive).error().code,
            Error::Code::kUnknownId);
}

TEST_F(RouterTest, ListsTheVenuesPairs) {
  fake_.AnswerListPairs(
      PairList{.symbols = {"BTC/USD", "ETH/USD"}, .depths = {10, 100}});
  Start();

  const std::expected<PairList, Error> pairs = router().ListPairs(Id::kKraken);

  ASSERT_TRUE(pairs);
  EXPECT_THAT(pairs->symbols, ElementsAre("BTC/USD", "ETH/USD"));
  EXPECT_THAT(pairs->depths, ElementsAre(10u, 100u));
}

TEST_F(RouterTest, PassesOnTheVenuesErrorListingPairs) {
  fake_.AnswerListPairs(std::unexpected{
      Error{.code = Error::Code::kVenueFailed, .detail = "unreachable"}});
  Start();

  const std::expected<PairList, Error> pairs = router().ListPairs(Id::kKraken);

  ASSERT_FALSE(pairs);
  EXPECT_THAT(pairs.error(),
              FieldsAre(Error::Code::kVenueFailed, "unreachable"));
}

TEST_F(RouterTest, RefusesToListPairsOfAVenueItHasNoManagerFor) {
  router_.emplace(Router::Managers{}, outbox_, kFastBackoff);

  const std::expected<PairList, Error> pairs = router().ListPairs(Id::kKraken);

  ASSERT_FALSE(pairs);
  EXPECT_EQ(pairs.error().code, Error::Code::kUnknownVenue);
}

TEST_F(RouterTest, ReturnsTheIdOfARecordingAlreadyRunning) {
  Start();
  ASSERT_EQ(router().Subscribe<Sink::kRecord>(
                Id::kKraken, {.symbol = "BTC/USD", .depth = 100}),
            kFirstRecord);
  fake_.AnswerNextSubscribe(kFirstRecord);

  EXPECT_EQ(router().Subscribe<Sink::kRecord>(
                Id::kKraken, {.symbol = "BTC/USD", .depth = 100}),
            kFirstRecord);
  EXPECT_TRUE(router().Unsubscribe(kFirstRecord));
}

TEST_F(RouterTest, UnsubscribesFromTheVenue) {
  Start();
  ASSERT_TRUE(router().Subscribe<Sink::kLive>(
      Id::kKraken, {.symbol = "BTC/USD", .depth = 100}));

  EXPECT_TRUE(router().Unsubscribe(kFirstLive));
  EXPECT_THAT(fake_.unsubscribed(), ElementsAre(kFirstLive));
  EXPECT_THAT(router().List(), IsEmpty());
}

TEST_F(RouterTest, ForgetsAnIdOnceUnsubscribed) {
  Start();
  ASSERT_TRUE(router().Subscribe<Sink::kLive>(
      Id::kKraken, {.symbol = "BTC/USD", .depth = 100}));
  ASSERT_TRUE(router().Unsubscribe(kFirstLive));

  EXPECT_EQ(router().Unsubscribe(kFirstLive).error().code,
            Error::Code::kUnknownId);
  EXPECT_THAT(fake_.unsubscribed(), SizeIs(1));
}

TEST_F(RouterTest, RefusesAnUnknownId) {
  Start();

  EXPECT_EQ(router()
                .Unsubscribe(MakeSubscriptionId(42, Id::kKraken, Sink::kLive))
                .error()
                .code,
            Error::Code::kUnknownId);
}

TEST_F(RouterTest, RefusesAnUnknownIdWithoutAnyVenue) {
  router_.emplace(Router::Managers{}, outbox_, kFastBackoff);

  EXPECT_EQ(router().Unsubscribe(kFirstLive).error().code,
            Error::Code::kUnknownId);
}

TEST_F(RouterTest, BuildsATransmitterForALiveFeed) {
  Start();
  ASSERT_TRUE(router().Subscribe<Sink::kLive>(
      Id::kKraken, {.symbol = "BTC/USD", .depth = 100}));

  EXPECT_THAT(
      dynamic_cast<frontend::Transmitter*>(fake_.BuildLastReceiver().get()),
      NotNull());
}

TEST_F(RouterTest, BuildsAFileWriterForARecording) {
  // FileWriter writes into the working directory, so the test works in one
  // of its own, which it leaves and removes however it ends.
  struct ScratchDirectory {
    ScratchDirectory()
        : original{std::filesystem::current_path()},
          path{std::filesystem::temp_directory_path() /
               std::format("data_feed_router_unittest-{}",
                           core::UnixNanosNow())} {
      std::filesystem::create_directories(path);
      std::filesystem::current_path(path);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory() {
      std::error_code ignored;
      std::filesystem::current_path(original, ignored);
      std::filesystem::remove_all(path, ignored);
    }

    std::filesystem::path original;
    std::filesystem::path path;
  };
  const ScratchDirectory scratch;
  Start();
  ASSERT_TRUE(router().Subscribe<Sink::kRecord>(
      Id::kKraken, {.symbol = "BTC/USD", .depth = 100}));

  // The writer is gone by the end of the statement, before the directory.
  EXPECT_THAT(dynamic_cast<l2::FileWriter*>(fake_.BuildLastReceiver().get()),
              NotNull());
}

TEST_F(RouterTest, ConnectsEveryVenue) {
  Start();

  EXPECT_TRUE(WaitUntil([this] { return fake_.healthy(); }));
}

TEST_F(RouterTest, DoesNotReconnectAHealthyVenue) {
  Start();
  ASSERT_TRUE(WaitUntil([this] { return fake_.healthy(); }));

  std::this_thread::sleep_for(std::chrono::milliseconds{300});

  EXPECT_THAT(fake_.connect_attempts(), SizeIs(1));
}

TEST_F(RouterTest, KeepsReconnectingUntilTheVenueAnswers) {
  fake_.FailConnects(3);
  Start();

  EXPECT_TRUE(WaitUntil([this] { return fake_.healthy(); }));
  EXPECT_THAT(fake_.connect_attempts(), SizeIs(4));
}

TEST_F(RouterTest, WaitsLongerAfterEachFailureUpToTheMaximum) {
  constexpr std::chrono::milliseconds kInitial{250};
  constexpr std::chrono::milliseconds kMax{400};
  fake_.FailConnects(3);
  Start(Backoff{.initial = kInitial, .max = kMax});
  ASSERT_TRUE(WaitUntil([this] { return fake_.healthy(); }));

  const std::vector<std::chrono::steady_clock::time_point> attempts =
      fake_.connect_attempts();
  ASSERT_THAT(attempts, SizeIs(4));
  const std::vector<std::chrono::steady_clock::duration> gaps =
      attempts |
      std::views::adjacent_transform<2>(
          [](std::chrono::steady_clock::time_point earlier,
             std::chrono::steady_clock::time_point later) {
            return later - earlier;
          }) |
      std::ranges::to<std::vector>();
  // 250 ms, then doubled to 500 but capped at 400, twice.
  EXPECT_THAT(gaps, ElementsAre(Ge(kInitial), Ge(kMax), Ge(kMax)));
  EXPECT_LT(gaps[0], kMax);
}

}  // namespace
}  // namespace venue
