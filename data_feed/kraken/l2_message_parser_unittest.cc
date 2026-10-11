#include "kraken/l2_message_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <mutex>
#include <optional>
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
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace kraken {
namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Test;
using ::testing::Truly;

constexpr l2::PairPrecision kBtcPrecision{.price_decimals = 1,
                                          .qty_decimals = 8};
// The depth every test pair is subscribed at.
constexpr u32 kDepth = 10;
constexpr l2::PairPrecision kEthPrecision{.price_decimals = 2,
                                          .qty_decimals = 8};

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

// One level as Kraken renders it: bare JSON numbers.
struct JsonLevel {
  std::string_view price;
  std::string_view qty;
};

// A frame of Kraken's v2 "book" channel.
struct BookFrame {
  std::string_view type = "update";
  std::string_view symbol = "BTC/USD";
  std::vector<JsonLevel> bids;
  std::vector<JsonLevel> asks;
  std::optional<u32> checksum;
  std::optional<std::string_view> timestamp;

  std::string ToJson() const {
    const auto side = [](const std::vector<JsonLevel>& levels) {
      return levels | std::views::transform([](const JsonLevel& level) {
               return std::format(R"({{"price":{},"qty":{}}})", level.price,
                                  level.qty);
             }) |
             std::views::join_with(',') | std::ranges::to<std::string>();
    };
    return std::format(
        R"({{"channel":"book","type":"{}","data":[{{"symbol":"{}",)"
        R"("bids":[{}],"asks":[{}]{}{}}}]}})",
        type, symbol, side(bids), side(asks),
        checksum ? std::format(R"(,"checksum":{})", *checksum) : "",
        timestamp ? std::format(R"(,"timestamp":"{}")", *timestamp) : "");
  }
};

// A minimal frame told apart from others by its checksum.
std::string Snapshot(u32 checksum, std::string_view symbol = "BTC/USD") {
  return BookFrame{.type = "snapshot", .symbol = symbol, .checksum = checksum}
      .ToJson();
}

std::string Update(u32 checksum, std::string_view symbol = "BTC/USD") {
  return BookFrame{.type = "update", .symbol = symbol, .checksum = checksum}
      .ToJson();
}

Matcher<l2::Message> HasChecksum(u32 checksum) {
  return Field("checksum", &l2::Message::checksum, checksum);
}

// Marks the "Fatal in ..." line the code under test is about to log as
// intended. Printed without a newline to the same stdout the log goes to, so
// both land on one line.
void AnnounceExpectedFatal() {
  std::print("[EXPECTED FATAL] ");
}

// Whether parsing `frame`, with a receiver attached for BTC/USD, marks the
// pipeline failed. Each call gets a parser of its own, since a failed parser
// stops parsing.
bool FailsThePipeline(const std::string& frame) {
  const std::unique_ptr<net::RawFrameQueue> frames =
      std::make_unique<net::RawFrameQueue>();
  core::PipelineStatus status;
  RecordingReceiver receiver;
  L2MessageParser parser{*frames, status};
  parser.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  AnnounceExpectedFatal();
  parser.Start();
  frames->push(net::RawFrame{.json = frame});
  parser.Stop();
  return status.failed && receiver.messages().empty();
}

class L2MessageParserTest : public Test {
 protected:
  // Runs the parser over `frames`, in order. Stop() parses every frame queued
  // before it, so all of them have been delivered once this returns.
  void Parse(const std::vector<std::string>& frames) {
    parser_.Start();
    for (const std::string& frame : frames) {
      Push(frame);
    }
    parser_.Stop();
  }

  void Push(std::string json, i64 capture_time_ns = 0) {
    ASSERT_TRUE(frames_->push(net::RawFrame{
        .json = std::move(json), .capture_time_ns = capture_time_ns}));
  }

  // Heap allocated: it holds its 256 frames inline.
  std::unique_ptr<net::RawFrameQueue> frames_ =
      std::make_unique<net::RawFrameQueue>();
  core::PipelineStatus status_;
  L2MessageParser parser_{*frames_, status_};
};

// A snapshot becomes a kSnapshot message for its pair, with every level's
// decimal text scaled to integers at the precision given on Attach().
TEST_F(L2MessageParserTest, DeliversASnapshotScaledToThePairsPrecision) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({BookFrame{.type = "snapshot",
                   .bids = {{.price = "84888.7", .qty = "0.06980997"},
                            {.price = "84888.6", .qty = "1.5"}},
                   .asks = {{.price = "84888.8", .qty = "12"}}}
             .ToJson()});

  EXPECT_THAT(
      receiver.messages(),
      ElementsAre(
          AllOf(Field("type", &l2::Message::type, l2::Message::Type::kSnapshot),
                Field("symbol", &l2::Message::symbol, "BTC/USD"),
                Field("buys", &l2::Message::buys,
                      ElementsAre(FieldsAre(848887, 6980997),
                                  FieldsAre(848886, 150000000))),
                Field("sells", &l2::Message::sells,
                      ElementsAre(FieldsAre(848888, 1200000000))))));
  EXPECT_FALSE(status_.failed);
}

// The checksum (even above the i32 range), the venue's timestamp and the
// capture time stamped by the network thread all reach the receiver.
TEST_F(L2MessageParserTest, CarriesTheChecksumAndBothTimestamps) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  parser_.Start();
  Push(BookFrame{.type = "snapshot",
                 .checksum = 2520196449u,
                 .timestamp = "1970-01-01T00:00:01.5Z"}
           .ToJson(),
       /*capture_time_ns=*/1'600'000'000);
  parser_.Stop();

  EXPECT_THAT(
      receiver.messages(),
      ElementsAre(AllOf(
          HasChecksum(2520196449u),
          Field("venue_time_ns", &l2::Message::venue_time_ns, 1'500'000'000),
          Field("capture_time_ns", &l2::Message::capture_time_ns,
                1'600'000'000))));
}

// A missing checksum or an unreadable timestamp is not fatal: the message is
// still delivered, with those fields zeroed.
TEST_F(L2MessageParserTest, ZeroesAMissingOrUnreadableChecksumAndTimestamp) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({BookFrame{.type = "snapshot"}.ToJson(),
         BookFrame{.type = "update", .timestamp = "yesterday"}.ToJson()});

  const Matcher<l2::Message> zeroed = AllOf(
      HasChecksum(0), Field("venue_time_ns", &l2::Message::venue_time_ns, 0));
  EXPECT_THAT(receiver.messages(), ElementsAre(zeroed, zeroed));
}

// Heartbeats, status updates, subscription acknowledgements, other channels
// and unparseable text are skipped without failing the pipeline.
TEST_F(L2MessageParserTest, IgnoresEverythingButBookFrames) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({
      R"({"channel":"heartbeat"})",
      R"({"channel":"status","type":"update","data":[{"system":"online"}]})",
      R"({"method":"subscribe","result":{"channel":"book","depth":10,)"
      R"("symbol":"BTC/USD"},"success":true})",
      R"({"channel":"trade","type":"update","data":[{"symbol":"BTC/USD"}]})",
      R"(["not", "an", "object"])",
      R"({"channel":"book", truncated)",
      "",
      Snapshot(1),
  });

  EXPECT_THAT(receiver.checksums(), ElementsAre(1));
  EXPECT_FALSE(status_.failed);
}

// A book frame whose layout is not the one Kraken documents fails the
// pipeline instead of aborting the process, and delivers nothing.
TEST_F(L2MessageParserTest, FailsThePipelineOnAMalformedBookFrame) {
  const std::vector<std::string> malformed_frames = {
      R"({"channel":"book","type":"snapshot"})",
      R"({"channel":"book","type":"snapshot","data":{}})",
      R"({"channel":"book","type":"snapshot","data":[]})",
      R"({"channel":"book","type":"snapshot","data":[42]})",
      R"({"channel":"book","data":[{"symbol":"BTC/USD","bids":[],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"bids":[],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":["BTC/USD"],)"
      R"("bids":[],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("bids":{},"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("bids":[{"qty":1}],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("bids":[{"price":{},"qty":1}],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("bids":[7],"asks":[]}]})",
      R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
      R"("bids":[],"asks":[],"checksum":{}}]})",
  };

  EXPECT_THAT(malformed_frames, Each(Truly(FailsThePipeline)));
}

// Frames for pairs nobody is attached to are dropped, so other pairs' traffic
// never reaches a receiver.
TEST_F(L2MessageParserTest, IgnoresPairsWithoutAReceiver) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({Snapshot(1, "ETH/USD"), Snapshot(2), Update(3, "ETH/USD")});

  EXPECT_THAT(receiver.checksums(), ElementsAre(2));
}

// A newly attached pair's stream starts with a snapshot: updates arriving
// before it are left over from an earlier subscription and are dropped.
TEST_F(L2MessageParserTest, DropsUpdatesUntilThePairsFirstSnapshot) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({Update(1), Update(2), Snapshot(3), Update(4), Update(5)});

  EXPECT_THAT(receiver.checksums(), ElementsAre(3, 4, 5));
}

// Each Start() is a new connection, so a pair's stream starts over at its
// next snapshot: updates before it belong to the connection that ended.
TEST_F(L2MessageParserTest, WaitsForAFreshSnapshotOnEveryStart) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({Snapshot(1), Update(2)});

  Parse({Update(3), Snapshot(4), Update(5)});

  EXPECT_THAT(receiver.checksums(), ElementsAre(1, 2, 4, 5));
}

// A pair has one receiver: attaching another is refused, and the first keeps
// streaming at its own precision.
TEST_F(L2MessageParserTest, RefusesASecondReceiverForAPair) {
  RecordingReceiver first;
  RecordingReceiver second;
  parser_.Attach("ETH/USD", kEthPrecision, kDepth, first);

  EXPECT_THROW(parser_.Attach("ETH/USD", kBtcPrecision, kDepth, second),
               std::invalid_argument);
  Parse({BookFrame{.type = "snapshot",
                   .symbol = "ETH/USD",
                   .asks = {{.price = "2696.29", .qty = "0.5"}}}
             .ToJson()});

  EXPECT_THAT(first.messages(),
              ElementsAre(Field("sells", &l2::Message::sells,
                                ElementsAre(FieldsAre(269629, 50000000)))));
  EXPECT_THAT(second.messages(), IsEmpty());
}

// Detaching a pair forgets it: its frames are dropped, and attaching to it
// again waits for a fresh snapshot.
TEST_F(L2MessageParserTest, ForgetsADetachedPair) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({Snapshot(1)});

  parser_.Detach("BTC/USD");
  Parse({Update(2)});
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);
  Parse({Update(3), Snapshot(4)});

  EXPECT_THAT(receiver.checksums(), ElementsAre(1, 4));
}

// Detaching a pair leaves the others streaming, and detaching one never
// attached changes nothing.
TEST_F(L2MessageParserTest, DetachesOnlyThePairItNames) {
  RecordingReceiver btc;
  RecordingReceiver eth;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, btc);
  parser_.Attach("ETH/USD", kEthPrecision, kDepth, eth);
  Parse({Snapshot(1), Snapshot(10, "ETH/USD")});

  parser_.Detach("ETH/USD");
  parser_.Detach("SOL/USD");
  // Each Parse() is a new connection, which starts with a snapshot.
  Parse({Snapshot(2), Snapshot(11, "ETH/USD")});

  EXPECT_THAT(btc.checksums(), ElementsAre(1, 2));
  EXPECT_THAT(eth.checksums(), ElementsAre(10));
}

// A receiver that throws takes the parser thread down, which is recorded in
// the shared PipelineStatus rather than escaping the thread.
TEST_F(L2MessageParserTest, MarksThePipelineFailedWhenAReceiverThrows) {
  ThrowingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  AnnounceExpectedFatal();
  Parse({Snapshot(1)});

  EXPECT_TRUE(status_.failed);
}

// Stop() on a parser that never started, or already stopped, does nothing.
TEST_F(L2MessageParserTest, CanBeStoppedWithoutEverStarting) {
  parser_.Stop();
  parser_.Stop();

  EXPECT_FALSE(status_.failed);
}

// The worked example of Kraken's book guide
// (https://docs.kraken.com/api/docs/guides/spot-ws-book-v2), whose checksum
// is kDocsChecksum.
const std::vector<JsonLevel> kDocsAsks = {
    {"45285.2", "0.00100000"}, {"45286.4", "1.54571953"},
    {"45286.6", "1.54571109"}, {"45289.6", "1.54560911"},
    {"45290.2", "0.15890660"}, {"45291.8", "1.54553491"},
    {"45294.7", "0.04454749"}, {"45296.1", "0.35380000"},
    {"45297.5", "0.09945542"}, {"45299.5", "0.18772827"},
};
const std::vector<JsonLevel> kDocsBids = {
    {"45283.5", "0.10000000"}, {"45283.4", "1.54582015"},
    {"45282.1", "0.10000000"}, {"45281.0", "0.10000000"},
    {"45280.3", "1.54592586"}, {"45279.0", "0.07990000"},
    {"45277.6", "0.03310103"}, {"45277.5", "0.30000000"},
    {"45277.3", "1.54602737"}, {"45276.6", "0.15445238"},
};
constexpr u32 kDocsChecksum = 3310070434;

std::string DocsSnapshot(std::optional<u32> checksum) {
  return BookFrame{.type = "snapshot",
                   .bids = kDocsBids,
                   .asks = kDocsAsks,
                   .checksum = checksum}
      .ToJson();
}

// With a mismatch handler, which records each pair it is told.
class L2MessageParserChecksumTest : public Test {
 protected:
  void Parse(const std::vector<std::string>& frames) {
    parser_.Start();
    for (const std::string& frame : frames) {
      ASSERT_TRUE(frames_->push(net::RawFrame{.json = frame}));
    }
    parser_.Stop();
  }

  std::unique_ptr<net::RawFrameQueue> frames_ =
      std::make_unique<net::RawFrameQueue>();
  core::PipelineStatus status_;
  // Written on the parser thread, read once Stop() has joined it.
  std::vector<std::string> mismatched_;
  L2MessageParser parser_{*frames_, status_, [this](std::string_view symbol) {
                            mismatched_.emplace_back(symbol);
                          }};
};

TEST_F(L2MessageParserChecksumTest, DeliversFramesWhoseChecksumMatches) {
  RecordingReceiver receiver;
  // Deeper than the checksum, so the update below lands outside its top ten.
  parser_.Attach("BTC/USD", kBtcPrecision, /*depth=*/25, receiver);

  Parse(
      {DocsSnapshot(kDocsChecksum),
       BookFrame{.asks = {{"45300.0", "1.00000000"}}, .checksum = kDocsChecksum}
           .ToJson()});

  EXPECT_THAT(receiver.messages(), ElementsAre(HasChecksum(kDocsChecksum),
                                               HasChecksum(kDocsChecksum)));
  EXPECT_THAT(receiver.gaps(), IsEmpty());
  EXPECT_THAT(mismatched_, IsEmpty());
  EXPECT_FALSE(status_.failed);
}

// A mismatch drops the frame, opens a gap and asks for the pair to be
// resubscribed; the pair's updates are then dropped until its new snapshot.
TEST_F(L2MessageParserChecksumTest, ResubscribesAPairWhoseChecksumFails) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({DocsSnapshot(kDocsChecksum + 1),
         BookFrame{.bids = {{"45283.5", "0.20000000"}}, .checksum = 7}.ToJson(),
         DocsSnapshot(kDocsChecksum)});

  EXPECT_THAT(receiver.messages(), ElementsAre(HasChecksum(kDocsChecksum)));
  EXPECT_THAT(receiver.gaps(), ElementsAre(core::Gap::kResubscribing));
  EXPECT_THAT(mismatched_, ElementsAre("BTC/USD"));
  // A mismatch is no reason to fail the pipeline.
  EXPECT_FALSE(status_.failed);
}

TEST_F(L2MessageParserChecksumTest, ChecksTheBookTheUpdatesLeaveBehind) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  // Changes a top level but claims the book is still the example's.
  Parse(
      {DocsSnapshot(kDocsChecksum),
       BookFrame{.bids = {{"45283.5", "0.20000000"}}, .checksum = kDocsChecksum}
           .ToJson()});

  EXPECT_THAT(receiver.messages(), ElementsAre(HasChecksum(kDocsChecksum)));
  EXPECT_THAT(mismatched_, ElementsAre("BTC/USD"));
}

TEST_F(L2MessageParserChecksumTest, ChecksNothingWithoutAChecksum) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, kDepth, receiver);

  Parse({DocsSnapshot(std::nullopt)});

  EXPECT_THAT(receiver.messages(), ElementsAre(HasChecksum(0)));
  EXPECT_THAT(mismatched_, IsEmpty());
}

}  // namespace
}  // namespace kraken
