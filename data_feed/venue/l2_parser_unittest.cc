#include "venue/l2_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <charconv>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "l2/book.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "venue/l2_decoder.h"

namespace venue {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Test;

constexpr l2::PairPrecision kBtcPrecision{.price_decimals = 1,
                                          .qty_decimals = 8};
constexpr l2::PairPrecision kEthPrecision{.price_decimals = 2,
                                          .qty_decimals = 8};
// The depth every test pair is subscribed at.
constexpr u32 kDepth = 10;

// Collects what the parser delivers. Called on the parser thread, read on the
// test thread once the parser has stopped.
class RecordingReceiver : public core::Receiver<l2::Message> {
 public:
  void Receive(l2::Message message) override {
    const std::scoped_lock lock{mutex_};
    messages_.push_back(std::move(message));
  }

  void OnGap(core::Gap gap) override {
    const std::scoped_lock lock{mutex_};
    gaps_.push_back(gap);
  }

  std::vector<l2::Message> messages() const {
    const std::scoped_lock lock{mutex_};
    return messages_;
  }

  std::vector<core::Gap> gaps() const {
    const std::scoped_lock lock{mutex_};
    return gaps_;
  }

  // The checksum of each message, which the tests use to tell frames apart.
  std::vector<u32> checksums() const {
    return messages() | std::views::transform(&l2::Message::checksum) |
           std::ranges::to<std::vector>();
  }

 private:
  mutable std::mutex mutex_;
  std::vector<l2::Message> messages_;
  std::vector<core::Gap> gaps_;
};

class ThrowingReceiver : public core::Receiver<l2::Message> {
 public:
  void Receive(l2::Message /*message*/) override {
    throw std::runtime_error{"receiver failed"};
  }

  void OnGap(core::Gap /*gap*/) override {}
};

// Decodes frames of a toy venue, one book change per frame, written as
// space-separated words: the type ("snapshot" or "update"), the symbol, the
// checksum, then levels as "b<price>" or "a<price>" at a quantity scaled to
// the pair's precision (so the precision the parser looked up shows), or
// "-b<price>" / "-a<price>" to remove one. "throw" throws.
class ToyDecoder : public L2Decoder {
 public:
  void Decode(const net::RawFrame& frame, Output& output) override {
    if (frame.json == "throw") {
      throw std::runtime_error{"malformed toy frame"};
    }
    const std::vector<std::string_view> words =
        frame.json | std::views::split(' ') |
        std::views::transform(
            [](auto word) { return std::string_view{word}; }) |
        std::ranges::to<std::vector>();
    const l2::Message::Type type = words.at(0) == "snapshot"
                                       ? l2::Message::Type::kSnapshot
                                       : l2::Message::Type::kUpdate;
    const l2::PairPrecision* precision = output.Find(words.at(1), type);
    if (precision == nullptr) {
      return;
    }
    l2::Message message{.type = type,
                        .checksum = ToNumber(words.at(2)),
                        .venue_time_ns = 0,
                        .capture_time_ns = frame.capture_time_ns,
                        .symbol = std::string{words.at(1)},
                        .buys = {},
                        .sells = {}};
    for (std::string_view level : words | std::views::drop(3)) {
      const bool removed = level.starts_with('-');
      level.remove_prefix(removed ? 1 : 0);
      (level.front() == 'b' ? message.buys : message.sells)
          .push_back(l2::Message::Level{
              .price = ToNumber(level.substr(1)),
              .quantity = removed ? 0 : precision->price_decimals});
    }
    output.Emit(std::move(message));
  }

 private:
  static u32 ToNumber(std::string_view text) {
    u32 number{0};
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
  }
};

// The toy venue's checksum: how many levels the book holds.
u32 LevelCount(const l2::Book& book) {
  return static_cast<u32>(book.bids().size() + book.asks().size());
}

// Marks the "Fatal in ..." line the code under test is about to log as
// intended. Printed without a newline to the same stdout the log goes to, so
// both land on one line.
void AnnounceExpectedFatal() {
  std::print("[EXPECTED FATAL] ");
}

class L2ParserTest : public Test {
 protected:
  // Runs the parser over `frames`, in order, as one connection. Stop() parses
  // every frame queued before it, so all of them have been delivered once
  // this returns.
  void Parse(const std::vector<std::string>& frames) {
    parser_.Start(std::make_unique<ToyDecoder>());
    for (const std::string& frame : frames) {
      ASSERT_TRUE(frames_->push(net::RawFrame{.json = frame}));
    }
    parser_.Stop();
  }

  // Heap allocated: it holds its 256 frames inline.
  std::unique_ptr<net::RawFrameQueue> frames_ =
      std::make_unique<net::RawFrameQueue>();
  core::PipelineStatus status_;
  L2Parser parser_{*frames_, status_};
};

// The decoder looks up the precision attached for the pair, and what it emits
// reaches the pair's receiver as it is.
TEST_F(L2ParserTest, DeliversWhatTheDecoderEmitsAtThePairsPrecision) {
  RecordingReceiver btc;
  RecordingReceiver eth;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, btc);
  parser_.Attach("ETH/USD", kEthPrecision, kDepth, eth);

  Parse({"snapshot BTC/USD 1 b100 a101", "snapshot ETH/USD 2 a7"});

  EXPECT_THAT(btc.messages(),
              ElementsAre(FieldsAre(l2::Message::Type::kSnapshot, 1u, 0, 0,
                                    "BTC/USD", ElementsAre(FieldsAre(100, 1)),
                                    ElementsAre(FieldsAre(101, 1)))));
  EXPECT_THAT(eth.messages(), ElementsAre(Field("sells", &l2::Message::sells,
                                                ElementsAre(FieldsAre(7, 2)))));
  EXPECT_FALSE(status_.failed);
}

// A decoder that throws fails the pipeline instead of escaping the thread.
TEST_F(L2ParserTest, FailsThePipelineWhenTheDecoderThrows) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  AnnounceExpectedFatal();
  Parse({"throw", "snapshot BTC/USD 1"});

  EXPECT_TRUE(status_.failed);
  EXPECT_THAT(receiver.messages(), IsEmpty());
}

// Book changes for pairs nobody is attached to are dropped, so other pairs'
// traffic never reaches a receiver.
TEST_F(L2ParserTest, IgnoresPairsWithoutAReceiver) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({"snapshot ETH/USD 1", "snapshot BTC/USD 2", "update ETH/USD 3"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(2));
}

// A newly attached pair's stream starts with a snapshot: updates arriving
// before it are left over from an earlier subscription and are dropped.
TEST_F(L2ParserTest, DropsUpdatesUntilThePairsFirstSnapshot) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({"update BTC/USD 1", "update BTC/USD 2", "snapshot BTC/USD 3",
         "update BTC/USD 4", "update BTC/USD 5"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(3, 4, 5));
}

// Each Start() is a new connection, so a pair's stream starts over at its
// next snapshot: updates before it belong to the connection that ended.
TEST_F(L2ParserTest, WaitsForAFreshSnapshotOnEveryStart) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({"snapshot BTC/USD 1", "update BTC/USD 2"});

  Parse({"update BTC/USD 3", "snapshot BTC/USD 4", "update BTC/USD 5"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(1, 2, 4, 5));
}

// A pair has one receiver: attaching another is refused, and the first keeps
// streaming at its own precision.
TEST_F(L2ParserTest, RefusesASecondReceiverForAPair) {
  RecordingReceiver first;
  RecordingReceiver second;
  parser_.Attach("ETH/USD", kEthPrecision, kDepth, first);

  EXPECT_THROW(parser_.Attach("ETH/USD", kBtcPrecision, kDepth, second),
               std::invalid_argument);
  Parse({"snapshot ETH/USD 1 a7"});

  EXPECT_THAT(first.messages(),
              ElementsAre(Field("sells", &l2::Message::sells,
                                ElementsAre(FieldsAre(7, 2)))));
  EXPECT_THAT(second.messages(), IsEmpty());
}

// Detaching a pair forgets it: its frames are dropped, and attaching to it
// again waits for a fresh snapshot.
TEST_F(L2ParserTest, ForgetsADetachedPair) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({"snapshot BTC/USD 1"});

  parser_.Detach("BTC/USD");
  Parse({"update BTC/USD 2"});
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({"update BTC/USD 3", "snapshot BTC/USD 4"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(1, 4));
}

// Detaching a pair leaves the others streaming, and detaching one never
// attached changes nothing.
TEST_F(L2ParserTest, DetachesOnlyThePairItNames) {
  RecordingReceiver btc;
  RecordingReceiver eth;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, btc);
  parser_.Attach("ETH/USD", kEthPrecision, kDepth, eth);
  Parse({"snapshot BTC/USD 1", "snapshot ETH/USD 10"});

  parser_.Detach("ETH/USD");
  parser_.Detach("SOL/USD");
  Parse({"snapshot BTC/USD 2", "snapshot ETH/USD 11"});

  EXPECT_THAT(btc.checksums(), ElementsAre(1, 2));
  EXPECT_THAT(eth.checksums(), ElementsAre(10));
}

// A receiver that throws takes the parser thread down, which is recorded in
// the shared PipelineStatus rather than escaping the thread.
TEST_F(L2ParserTest, MarksThePipelineFailedWhenAReceiverThrows) {
  ThrowingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  AnnounceExpectedFatal();
  Parse({"snapshot BTC/USD 1"});

  EXPECT_TRUE(status_.failed);
}

// Stop() on a parser that never started, or already stopped, does nothing.
TEST_F(L2ParserTest, CanBeStoppedWithoutEverStarting) {
  parser_.Stop();
  parser_.Stop();

  EXPECT_FALSE(status_.failed);
}

// With a ChecksumCheck counting the book's levels, which records each pair
// its handler is told.
class L2ParserChecksumTest : public L2ParserTest {
 protected:
  // Written on the parser thread, read once Stop() has joined it.
  std::vector<std::string> mismatched_;
  L2Parser checked_parser_{
      *frames_, status_,
      L2Parser::ChecksumCheck{.checksum = &LevelCount,
                              .on_mismatch = [this](std::string_view symbol) {
                                mismatched_.emplace_back(symbol);
                              }}};

  void ParseChecked(const std::vector<std::string>& frames) {
    checked_parser_.Start(std::make_unique<ToyDecoder>());
    for (const std::string& frame : frames) {
      ASSERT_TRUE(frames_->push(net::RawFrame{.json = frame}));
    }
    checked_parser_.Stop();
  }
};

// Book changes whose checksum matches the book they leave behind are
// delivered.
TEST_F(L2ParserChecksumTest, DeliversChangesWhoseChecksumMatches) {
  RecordingReceiver receiver;
  checked_parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  ParseChecked({"snapshot BTC/USD 2 b100 a101", "update BTC/USD 3 b99",
                "update BTC/USD 2 -b99"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(2, 3, 2));
  EXPECT_THAT(receiver.gaps(), IsEmpty());
  EXPECT_THAT(mismatched_, IsEmpty());
}

// A mismatch drops the change, opens a gap and asks for the pair to be
// resubscribed; the pair's updates are then dropped until its new snapshot.
TEST_F(L2ParserChecksumTest, ResubscribesAPairWhoseChecksumFails) {
  RecordingReceiver receiver;
  checked_parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  ParseChecked({"snapshot BTC/USD 2 b100 a101", "update BTC/USD 7 b99",
                "update BTC/USD 3 b98", "snapshot BTC/USD 1 b100"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(2, 1));
  EXPECT_THAT(receiver.gaps(), ElementsAre(core::Gap::kResubscribing));
  EXPECT_THAT(mismatched_, ElementsAre("BTC/USD"));
  // A mismatch is no reason to fail the pipeline.
  EXPECT_FALSE(status_.failed);
}

// The book is kept at the subscribed depth, so the checksum covers what the
// venue keeps rather than every level ever sent.
TEST_F(L2ParserChecksumTest, ChecksTheBookAtTheSubscribedDepth) {
  RecordingReceiver receiver;
  checked_parser_.Attach("BTC/USD", kBtcPrecision, /*depth=*/1, receiver);

  ParseChecked({"snapshot BTC/USD 2 b100 b99 a101"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(2));
  EXPECT_THAT(mismatched_, IsEmpty());
}

// A change without a checksum (zero) is not checked.
TEST_F(L2ParserChecksumTest, ChecksNothingWithoutAChecksum) {
  RecordingReceiver receiver;
  checked_parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  ParseChecked({"snapshot BTC/USD 0 b100 a101"});

  EXPECT_THAT(receiver.checksums(), ElementsAre(0));
  EXPECT_THAT(mismatched_, IsEmpty());
}

}  // namespace
}  // namespace venue
