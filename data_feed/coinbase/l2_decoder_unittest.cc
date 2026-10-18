#include "coinbase/l2_decoder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <exception>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "venue/l2_decoder.h"

namespace coinbase {
namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Truly;

constexpr l2::PairPrecision kBtcPrecision{.price_decimals = 2,
                                          .qty_decimals = 8};

// Hands the decoder BTC-USD's precision, and keeps what it emits.
class RecordingOutput : public venue::L2Decoder::Output {
 public:
  const l2::PairPrecision* Find(std::string_view symbol,
                                l2::Message::Type /*type*/) const override {
    return symbol == "BTC-USD" ? &kBtcPrecision : nullptr;
  }

  void Emit(l2::Message message) override {
    messages.push_back(std::move(message));
  }

  std::vector<l2::Message> messages;
};

// One entry of a level2 event's "updates".
struct Update {
  std::string_view side = "bid";
  std::string_view price = "100.00";
  std::string_view quantity = "1";
};

// One entry of an "l2_data" frame's "events".
struct Event {
  std::string_view type = "update";
  std::string_view product = "BTC-USD";
  std::vector<Update> updates;

  std::string ToJson() const {
    return std::format(
        R"({{"type":"{}","product_id":"{}","updates":[{}]}})", type, product,
        updates | std::views::transform([](const Update& update) {
          return std::format(
              R"({{"side":"{}","event_time":"2026-10-18T12:00:00Z",)"
              R"("price_level":"{}","new_quantity":"{}"}})",
              update.side, update.price, update.quantity);
        }) | std::views::join_with(',') |
            std::ranges::to<std::string>());
  }
};

// A frame of the "level2" channel, numbered `sequence` on its connection.
std::string L2Data(u64 sequence,
                   const std::vector<Event>& events,
                   std::string_view timestamp = "1970-01-01T00:00:01.5Z") {
  return std::format(R"({{"channel":"l2_data","client_id":"","timestamp":"{}",)"
                     R"("sequence_num":{},"events":[{}]}})",
                     timestamp, sequence,
                     events | std::views::transform(&Event::ToJson) |
                         std::views::join_with(',') |
                         std::ranges::to<std::string>());
}

// A frame of the "heartbeats" channel.
std::string Heartbeat(u64 sequence, u64 counter) {
  return std::format(
      R"({{"channel":"heartbeats","client_id":"","timestamp":)"
      R"("2026-10-18T12:00:00Z","sequence_num":{},"events":[{{"current_time":)"
      R"("2026-10-18 12:00:00 +0000 UTC","heartbeat_counter":{}}}]}})",
      sequence, counter);
}

// What decoding `frames`, in order, on one connection emits.
std::vector<l2::Message> Decode(const std::vector<std::string>& frames) {
  L2Decoder decoder;
  RecordingOutput output;
  for (const std::string& frame : frames) {
    decoder.Decode(net::RawFrame{.json = frame}, output);
  }
  return std::move(output.messages);
}

// Whether decoding `frames` on one connection throws at the last one, and
// only there.
bool ThrowsAtTheLast(const std::vector<std::string>& frames) {
  L2Decoder decoder;
  RecordingOutput output;
  for (const std::string& frame :
       frames | std::views::take(frames.size() - 1)) {
    decoder.Decode(net::RawFrame{.json = frame}, output);
  }
  try {
    decoder.Decode(net::RawFrame{.json = frames.back()}, output);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

bool Throws(const std::string& frame) {
  return ThrowsAtTheLast({frame});
}

// Each event of a frame is one book change of its product: bids go to buys,
// offers to sells, scaled at the product's precision. The venue time is the
// frame's timestamp, and Coinbase sends no checksum.
TEST(CoinbaseL2DecoderTest, EmitsEachEventAsABookChange) {
  L2Decoder decoder;
  RecordingOutput output;

  decoder.Decode(
      net::RawFrame{
          .json = L2Data(1, {Event{.type = "snapshot",
                                   .updates = {{.side = "bid",
                                                .price = "84888.71",
                                                .quantity = "0.06980997"},
                                               {.side = "offer",
                                                .price = "84888.8",
                                                .quantity = "12"}}},
                             Event{.updates = {{.side = "bid",
                                                .price = "84888.71",
                                                .quantity = "0"}}}}),
          .capture_time_ns = 1'600'000'000},
      output);

  EXPECT_THAT(
      output.messages,
      ElementsAre(
          FieldsAre(l2::Message::Type::kSnapshot, 0u, 1'500'000'000,
                    1'600'000'000, "BTC-USD",
                    ElementsAre(FieldsAre(8488871, 6980997)),
                    ElementsAre(FieldsAre(8488880, 1200000000))),
          AllOf(Field("type", &l2::Message::type, l2::Message::Type::kUpdate),
                Field("buys", &l2::Message::buys,
                      ElementsAre(FieldsAre(8488871, 0))),
                Field("sells", &l2::Message::sells, IsEmpty()))));
}

// Coinbase pads prices and sizes past the product's increments with zeros;
// they carry no precision, so they scale like the short form.
TEST(CoinbaseL2DecoderTest, AcceptsTrailingZerosPastThePrecision) {
  EXPECT_THAT(
      Decode({L2Data(1, {Event{.updates = {{.price = "22356.270000",
                                            .quantity = "0.0000000000"}}}})}),
      ElementsAre(Field("buys", &l2::Message::buys,
                        ElementsAre(FieldsAre(2235627, 0)))));
}

// Events for products the output does not want are skipped before their
// levels are read, while the frame's other events still go out.
TEST(CoinbaseL2DecoderTest, SkipsProductsTheOutputDoesNotWant) {
  EXPECT_THAT(Decode({L2Data(1, {Event{.product = "ETH-USD",
                                       .updates = {{.side = "nonsense"}}},
                                 Event{.type = "snapshot"}})}),
              ElementsAre(Field("symbol", &l2::Message::symbol, "BTC-USD")));
}

// Subscription acknowledgements, heartbeats, other channels, error reports
// and unparseable text emit nothing and do not throw.
TEST(CoinbaseL2DecoderTest, IgnoresEverythingButLevel2Frames) {
  EXPECT_THAT(
      Decode({
          R"({"channel":"subscriptions","client_id":"","timestamp":)"
          R"("2026-10-18T12:00:00Z","sequence_num":0,"events":[)"
          R"({"subscriptions":{"level2":["BTC-USD"]}}]})",
          Heartbeat(1, 7),
          R"({"channel":"ticker","timestamp":"2026-10-18T12:00:00Z",)"
          R"("sequence_num":2,"events":[{"type":"update","tickers":[]}]})",
          R"({"type":"error","message":"Failed to subscribe"})",
          R"(["not", "an", "object"])",
          R"({"channel":"l2_data", truncated)",
          "",
          L2Data(3, {Event{.type = "snapshot"}}),
      }),
      ElementsAre(
          Field("type", &l2::Message::type, l2::Message::Type::kSnapshot)));
}

// sequence_num counts every frame of the connection, whatever its channel,
// so a skipped number is a gap for every product: decoding throws, which
// rebuilds the connection.
TEST(CoinbaseL2DecoderTest, ThrowsOnASequenceGap) {
  EXPECT_TRUE(ThrowsAtTheLast(
      {Heartbeat(4, 1), L2Data(5, {Event{}}), L2Data(7, {Event{}})}));
}

// A frame numbered at or below the last one is out of order and skipped.
TEST(CoinbaseL2DecoderTest, SkipsAFrameOutOfOrder) {
  EXPECT_THAT(Decode({L2Data(1, {Event{.type = "snapshot"}}),
                      L2Data(2, {Event{.updates = {{.price = "1"}}}}),
                      L2Data(2, {Event{.updates = {{.price = "2"}}}}),
                      L2Data(1, {Event{.updates = {{.price = "3"}}}}),
                      L2Data(3, {Event{.updates = {{.price = "4"}}}})}),
              ElementsAre(Field("buys", &l2::Message::buys, IsEmpty()),
                          Field("buys", &l2::Message::buys,
                                ElementsAre(FieldsAre(100, 100000000))),
                          Field("buys", &l2::Message::buys,
                                ElementsAre(FieldsAre(400, 100000000)))));
}

// A heartbeat counter that skips means heartbeats were lost, so decoding
// throws.
TEST(CoinbaseL2DecoderTest, ThrowsWhenHeartbeatsSkip) {
  EXPECT_FALSE(ThrowsAtTheLast({Heartbeat(1, 10), Heartbeat(2, 11)}));
  EXPECT_TRUE(ThrowsAtTheLast({Heartbeat(1, 10), Heartbeat(2, 12)}));
}

// A level2 frame Coinbase does not document, or a level no book of the
// product can hold, throws and emits nothing.
TEST(CoinbaseL2DecoderTest, ThrowsOnAMalformedFrame) {
  const std::vector<std::string> malformed_frames = {
      R"({"channel":"l2_data","events":[]})",
      R"({"channel":"l2_data","sequence_num":"one","events":[]})",
      R"({"channel":"l2_data","sequence_num":1})",
      R"({"channel":"l2_data","sequence_num":1,"events":{}})",
      R"({"channel":"heartbeats","sequence_num":1,"events":[{}]})",
      R"({"channel":"l2_data","sequence_num":1,"events":[)"
      R"({"type":"update","updates":[]}]})",
      R"({"channel":"l2_data","sequence_num":1,"events":[)"
      R"({"product_id":"BTC-USD","updates":[]}]})",
      R"({"channel":"l2_data","sequence_num":1,"events":[)"
      R"({"type":"update","product_id":"BTC-USD","updates":{}}]})",
      L2Data(1, {Event{.type = "delta"}}),
      L2Data(1, {Event{.updates = {{.side = "ask"}}}}),
      L2Data(1, {Event{.updates = {{.quantity = "-1"}}}}),
      // Finer than BTC-USD's cent: its increments changed.
      L2Data(1, {Event{.updates = {{.price = "100.001"}}}}),
      L2Data(1, {Event{.updates = {{.quantity = "0.000000001"}}}}),
  };

  EXPECT_THAT(malformed_frames, Each(Truly(Throws)));
}

}  // namespace
}  // namespace coinbase
