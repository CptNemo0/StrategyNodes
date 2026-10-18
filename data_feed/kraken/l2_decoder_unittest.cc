#include "kraken/l2_decoder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "kraken/checksum.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"

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

// Hands the decoder BTC/USD's precision, and keeps what it emits.
class RecordingOutput : public venue::L2Decoder::Output {
 public:
  const l2::PairPrecision* Find(std::string_view symbol,
                                l2::Message::Type /*type*/) const override {
    return symbol == "BTC/USD" ? &kBtcPrecision : nullptr;
  }

  void Emit(l2::Message message) override {
    messages.push_back(std::move(message));
  }

  std::vector<l2::Message> messages;
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

std::string Snapshot(u32 checksum, std::string_view symbol = "BTC/USD") {
  return BookFrame{.type = "snapshot", .symbol = symbol, .checksum = checksum}
      .ToJson();
}

Matcher<l2::Message> HasChecksum(u32 checksum) {
  return Field("checksum", &l2::Message::checksum, checksum);
}

// What decoding `frames`, in order, emits.
std::vector<l2::Message> Decode(const std::vector<std::string>& frames) {
  L2Decoder decoder;
  RecordingOutput output;
  for (const std::string& frame : frames) {
    decoder.Decode(net::RawFrame{.json = frame}, output);
  }
  return std::move(output.messages);
}

// Whether decoding `frame` throws, which fails the pipeline.
bool Throws(const std::string& frame) {
  L2Decoder decoder;
  RecordingOutput output;
  try {
    decoder.Decode(net::RawFrame{.json = frame}, output);
  } catch (const std::exception&) {
    return output.messages.empty();
  }
  return false;
}

// A snapshot becomes a kSnapshot message for its pair, with every level's
// decimal text scaled to integers at the precision the output gives.
TEST(KrakenL2DecoderTest, EmitsASnapshotScaledToThePairsPrecision) {
  EXPECT_THAT(
      Decode({BookFrame{.type = "snapshot",
                        .bids = {{.price = "84888.7", .qty = "0.06980997"},
                                 {.price = "84888.6", .qty = "1.5"}},
                        .asks = {{.price = "84888.8", .qty = "12"}}}
                  .ToJson()}),
      ElementsAre(
          AllOf(Field("type", &l2::Message::type, l2::Message::Type::kSnapshot),
                Field("symbol", &l2::Message::symbol, "BTC/USD"),
                Field("buys", &l2::Message::buys,
                      ElementsAre(FieldsAre(848887, 6980997),
                                  FieldsAre(848886, 150000000))),
                Field("sells", &l2::Message::sells,
                      ElementsAre(FieldsAre(848888, 1200000000))))));
}

// The checksum (even above the i32 range), the venue's timestamp and the
// capture time stamped by the network thread all reach the message.
TEST(KrakenL2DecoderTest, CarriesTheChecksumAndBothTimestamps) {
  L2Decoder decoder;
  RecordingOutput output;

  decoder.Decode(
      net::RawFrame{.json = BookFrame{.type = "snapshot",
                                      .checksum = 2520196449u,
                                      .timestamp = "1970-01-01T00:00:01.5Z"}
                                .ToJson(),
                    .capture_time_ns = 1'600'000'000},
      output);

  EXPECT_THAT(
      output.messages,
      ElementsAre(AllOf(
          HasChecksum(2520196449u),
          Field("venue_time_ns", &l2::Message::venue_time_ns, 1'500'000'000),
          Field("capture_time_ns", &l2::Message::capture_time_ns,
                1'600'000'000))));
}

// A missing checksum or an unreadable timestamp is not fatal: the message is
// still emitted, with those fields zeroed.
TEST(KrakenL2DecoderTest, ZeroesAMissingOrUnreadableChecksumAndTimestamp) {
  const Matcher<l2::Message> zeroed = AllOf(
      HasChecksum(0), Field("venue_time_ns", &l2::Message::venue_time_ns, 0));

  EXPECT_THAT(
      Decode({BookFrame{.type = "snapshot"}.ToJson(),
              BookFrame{.type = "update", .timestamp = "yesterday"}.ToJson()}),
      ElementsAre(zeroed, zeroed));
}

// Heartbeats, status updates, subscription acknowledgements, other channels
// and unparseable text are skipped without throwing.
TEST(KrakenL2DecoderTest, IgnoresEverythingButBookFrames) {
  EXPECT_THAT(
      Decode({
          R"({"channel":"heartbeat"})",
          R"({"channel":"status","type":"update","data":[{"system":"online"}]})",
          R"({"method":"subscribe","result":{"channel":"book","depth":10,)"
          R"("symbol":"BTC/USD"},"success":true})",
          R"({"channel":"trade","type":"update","data":[{"symbol":"BTC/USD"}]})",
          R"(["not", "an", "object"])",
          R"({"channel":"book", truncated)",
          "",
          Snapshot(1),
      }),
      ElementsAre(HasChecksum(1)));
}

// Book frames for pairs the output does not want are skipped before their
// levels are read.
TEST(KrakenL2DecoderTest, SkipsPairsTheOutputDoesNotWant) {
  EXPECT_THAT(
      Decode({Snapshot(1, "ETH/USD"),
              R"({"channel":"book","type":"snapshot","data":[{"symbol":)"
              R"("ETH/USD","bids":{},"asks":[]}]})",
              Snapshot(2)}),
      ElementsAre(HasChecksum(2)));
}

// A book frame whose layout is not the one Kraken documents throws, and
// emits nothing.
TEST(KrakenL2DecoderTest, ThrowsOnAMalformedBookFrame) {
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

  EXPECT_THAT(malformed_frames, Each(Truly(Throws)));
}

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

 private:
  mutable std::mutex mutex_;
  std::vector<l2::Message> messages_;
  std::vector<core::Gap> gaps_;
};

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

// Kraken's frames through venue::L2Parser checked with BookChecksum, as
// venue::L2Manager runs them, recording each pair the mismatch handler is
// told.
class KrakenChecksumTest : public Test {
 protected:
  void Parse(const std::vector<std::string>& frames) {
    parser_.Start(std::make_unique<L2Decoder>());
    for (const std::string& frame : frames) {
      ASSERT_TRUE(frames_->push(net::RawFrame{.json = frame}));
    }
    parser_.Stop();
  }

  // Heap allocated: it holds its 256 frames inline.
  std::unique_ptr<net::RawFrameQueue> frames_ =
      std::make_unique<net::RawFrameQueue>();
  core::PipelineStatus status_;
  // Written on the parser thread, read once Stop() has joined it.
  std::vector<std::string> mismatched_;
  venue::L2Parser parser_{*frames_, status_,
                          venue::L2Parser::ChecksumCheck{
                              .checksum = &BookChecksum,
                              .on_mismatch = [this](std::string_view symbol) {
                                mismatched_.emplace_back(symbol);
                              }}};
};

// Frames that leave the book Kraken checksummed are delivered, even when
// they change levels deeper than the checksum covers.
TEST_F(KrakenChecksumTest, DeliversFramesWhoseChecksumMatches) {
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

// An update that changes a top level but claims the book is unchanged fails
// the check, so the pair is resubscribed.
TEST_F(KrakenChecksumTest, ChecksTheBookTheUpdatesLeaveBehind) {
  RecordingReceiver receiver;
  parser_.Attach("BTC/USD", kBtcPrecision, /*depth=*/10, receiver);

  Parse(
      {DocsSnapshot(kDocsChecksum),
       BookFrame{.bids = {{"45283.5", "0.20000000"}}, .checksum = kDocsChecksum}
           .ToJson()});

  EXPECT_THAT(receiver.messages(), ElementsAre(HasChecksum(kDocsChecksum)));
  EXPECT_THAT(receiver.gaps(), ElementsAre(core::Gap::kResubscribing));
  EXPECT_THAT(mismatched_, ElementsAre("BTC/USD"));
}

}  // namespace
}  // namespace kraken
