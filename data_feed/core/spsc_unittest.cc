#include "core/spsc.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <cstddef>
#include <print>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/pipeline_status.h"

namespace core {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

constexpr std::size_t kCapacity = 4;

using IntQueue =
    boost::lockfree::spsc_queue<int, boost::lockfree::capacity<kCapacity>>;

// 0, 1, ..., count - 1: the values Fill() and the producers below push.
std::vector<int> Iota(std::size_t count) {
  return std::views::iota(0, static_cast<int>(count)) |
         std::ranges::to<std::vector>();
}

void Fill(IntQueue& queue) {
  for (const int value : Iota(kCapacity)) {
    ASSERT_TRUE(queue.push(value));
  }
}

std::vector<int> PopAll(IntQueue& queue) {
  std::vector<int> values;
  queue.consume_all([&values](int value) { values.push_back(value); });
  return values;
}

// Marks the "Fatal in ..." line the code under test is about to log as
// intended. Printed without a newline to the same stdout the log goes to, so
// both land on one line.
void AnnounceExpectedFatal() {
  std::print("[EXPECTED FATAL] ");
}

// With room in the queue, a push lands immediately.
TEST(PushBlockingTest, PushesWhileThereIsRoom) {
  IntQueue queue;
  const std::atomic<bool> give_up{false};

  PushBlocking(queue, 7, give_up);

  EXPECT_THAT(PopAll(queue), ElementsAre(7));
}

// On a full queue the producer waits instead of dropping, and its value
// lands behind the queued ones once the consumer frees a slot.
TEST(PushBlockingTest, WaitsForTheConsumerToFreeASlot) {
  IntQueue queue;
  Fill(queue);
  const std::atomic<bool> give_up{false};
  std::atomic<bool> pushed{false};

  std::jthread producer{[&] {
    PushBlocking(queue, 100, give_up);
    pushed = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(pushed) << "pushed into a full queue";

  int front{-1};
  ASSERT_TRUE(queue.pop(front));
  producer.join();

  EXPECT_TRUE(pushed);
  EXPECT_THAT(PopAll(queue), ElementsAre(1, 2, 3, 100));
}

// A producer blocked on a full queue returns once the give-up flag is set,
// leaving the queue as it was.
TEST(PushBlockingTest, GivesUpOnAFullQueueOnceTheFlagIsSet) {
  IntQueue queue;
  Fill(queue);
  PipelineStatus status;

  std::jthread producer{[&] { PushBlocking(queue, 100, status.failed); }};
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  status.Fail();
  producer.join();

  EXPECT_THAT(PopAll(queue), ElementsAreArray(Iota(kCapacity)));
}

// The give-up flag only ends waiting; a queue with room still takes the
// value.
TEST(PushBlockingTest, StillPushesIntoRoomAfterGivingUp) {
  IntQueue queue;
  const std::atomic<bool> give_up{true};

  PushBlocking(queue, 7, give_up);

  EXPECT_THAT(PopAll(queue), ElementsAre(7));
}

// A stop requested up front still drains what is already queued.
TEST(DrainTest, ConsumesEverythingQueuedBeforeTheStop) {
  IntQueue queue;
  Fill(queue);
  std::stop_source stop;
  stop.request_stop();
  std::vector<int> consumed;
  bool failed{false};

  Drain(
      stop.get_token(), "test", queue,
      [&consumed](int value) { consumed.push_back(value); },
      [&failed] { failed = true; });

  EXPECT_THAT(consumed, ElementsAreArray(Iota(kCapacity)));
  EXPECT_FALSE(failed);
}

// Under sustained back-pressure, every value pushed before the stop is
// consumed exactly once, in order.
TEST(DrainTest, ConsumesInOrderUntilStoppedWithoutLosingAnything) {
  constexpr std::size_t kMessageCount = 1000;
  IntQueue queue;
  const std::atomic<bool> give_up{false};
  std::vector<int> consumed;
  bool failed{false};

  {
    const std::jthread consumer{[&](const std::stop_token& stop) {
      Drain(
          stop, "test", queue,
          [&consumed](int value) { consumed.push_back(value); },
          [&failed] { failed = true; });
    }};
    // Far more than the queue holds, so the producer must wait on the
    // consumer over and over.
    for (const int value : Iota(kMessageCount)) {
      PushBlocking(queue, value, give_up);
    }
    // The jthread requests the stop and joins here.
  }

  EXPECT_THAT(consumed, ElementsAreArray(Iota(kMessageCount)));
  EXPECT_FALSE(failed);
}

// A consumer that throws stops the drain and is reported through on_fail
// exactly once; the failing value and what follows stay queued.
TEST(DrainTest, ReportsAThrowingConsumerAndStopsConsuming) {
  IntQueue queue;
  Fill(queue);
  const std::stop_source never_stopped;
  std::vector<int> consumed;
  int failures{0};

  AnnounceExpectedFatal();
  Drain(
      never_stopped.get_token(), "test", queue,
      [&consumed](int value) {
        if (value == 2) {
          throw std::runtime_error{"bad message"};
        }
        consumed.push_back(value);
      },
      [&failures] { ++failures; });

  EXPECT_THAT(consumed, ElementsAre(0, 1));
  EXPECT_EQ(failures, 1);
  // The bad message and what follows it are left for whoever recovers the
  // pipeline (e.g. venue::L2Manager resets the queue).
  EXPECT_THAT(PopAll(queue), ElementsAre(2, 3));
}

}  // namespace
}  // namespace core
