#include "l2/message.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/aliasing.h"

namespace l2 {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

// Bytes of the fixed prefix: type, checksum, both times and the buys count.
constexpr u64 kEmptyMessageSize = 4 + 4 + 8 + 8 + 8 + 8;

// Reads serialized fields back in order, the way a replay would.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> bytes) : rest_{bytes} {}

  template <typename T>
  T Read() {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    std::memcpy(&value, rest_.data(), sizeof(T));
    rest_ = rest_.subspan(sizeof(T));
    return value;
  }

  // Skips the type, checksum and both times.
  void SkipPrefix() {
    Read<Message::Type>();
    Read<u32>();
    Read<i64>();
    Read<i64>();
  }

  // Reads a side as Serialize writes it: a byte count, then the levels.
  std::vector<Message::Level> ReadSide() {
    std::vector<Message::Level> levels(Read<u64>() / sizeof(Message::Level));
    for (Message::Level& level : levels) {
      level = Read<Message::Level>();
    }
    return levels;
  }

  std::span<const std::byte> rest() const { return rest_; }

 private:
  std::span<const std::byte> rest_;
};

Message MakeMessage() {
  return Message{.type = Message::Type::kSnapshot,
                 .checksum = 2520196449u,
                 .venue_time_ns = 1'790'508'579'799'080'000,
                 .capture_time_ns = 1'790'508'579'812'345'678,
                 .symbol = "BTC/USD",
                 .buys = {{.price = 8488870, .quantity = 6980997},
                          {.price = 8488860, .quantity = 100000000}},
                 .sells = {{.price = 8488880, .quantity = 1}}};
}

std::vector<std::byte> SerializeToVector(const Message& message) {
  std::vector<std::byte> bytes(message.GetByteSize());
  message.Serialize(bytes);
  return bytes;
}

// A message without levels serializes to the fixed prefix only.
TEST(MessageTest, AnEmptyBookTakesOnlyTheFixedPrefix) {
  const Message message{.type = Message::Type::kUpdate,
                        .checksum = 0,
                        .venue_time_ns = 0,
                        .capture_time_ns = 0,
                        .symbol = "BTC/USD",
                        .buys = {},
                        .sells = {}};

  EXPECT_EQ(message.GetByteSize(), kEmptyMessageSize);
  EXPECT_THAT(SerializeToVector(message), SizeIs(kEmptyMessageSize));
}

// Every level, buy or sell, adds one price and one quantity.
TEST(MessageTest, EachLevelAddsSixteenBytes) {
  EXPECT_EQ(MakeMessage().GetByteSize(), kEmptyMessageSize + 3 * 16);
}

// The bytes follow the documented on-disk layout field by field, so a
// replay can read them back.
TEST(MessageTest, SerializesInTheDocumentedLayout) {
  const Message message = MakeMessage();
  const std::vector<std::byte> bytes = SerializeToVector(message);

  ByteReader reader{bytes};
  EXPECT_EQ(reader.Read<Message::Type>(), Message::Type::kSnapshot);
  EXPECT_EQ(reader.Read<u32>(), message.checksum);
  EXPECT_EQ(reader.Read<i64>(), message.venue_time_ns);
  EXPECT_EQ(reader.Read<i64>(), message.capture_time_ns);
  EXPECT_THAT(reader.ReadSide(), ElementsAre(FieldsAre(8488870, 6980997),
                                             FieldsAre(8488860, 100000000)));
  EXPECT_THAT(reader.ReadSide(), ElementsAre(FieldsAre(8488880, 1)));
  EXPECT_THAT(reader.rest(), IsEmpty());
}

// Both level arrays start 8 byte aligned, so a reader can map them in
// place.
TEST(MessageTest, KeepsBothSidesEightByteAligned) {
  const std::vector<std::byte> bytes = SerializeToVector(MakeMessage());

  // The buys start right after the 24 byte prefix and their count.
  constexpr std::size_t kBuysOffset = 24 + 8;
  constexpr std::size_t kSellsOffset = kBuysOffset + 2 * 16 + 8;
  static_assert(kBuysOffset % 8 == 0 && kSellsOffset % 8 == 0);

  Message::Level first_sell{};
  std::memcpy(&first_sell, bytes.data() + kSellsOffset, sizeof(first_sell));
  EXPECT_THAT(first_sell, FieldsAre(8488880, 1));
}

// Serialize() writes exactly GetByteSize() bytes and touches nothing past
// them.
TEST(MessageTest, ReturnsTheBytesWrittenAndLeavesTheRestOfTheBufferAlone) {
  const Message message = MakeMessage();
  constexpr std::byte kUntouched{0xAB};
  std::vector<std::byte> buffer(message.GetByteSize() + 16, kUntouched);

  EXPECT_EQ(message.Serialize(buffer), message.GetByteSize());
  EXPECT_THAT(std::span{buffer}.subspan(message.GetByteSize()),
              Each(kUntouched));
}

// A buffer too small is refused instead of overrun.
TEST(MessageTest, RefusesABufferTooSmall) {
  const Message message = MakeMessage();
  std::vector<std::byte> buffer(message.GetByteSize() - 1);

  EXPECT_THROW(message.Serialize(buffer), std::length_error);
}

// The symbol is not serialized: it lives in the file header.
TEST(MessageTest, LeavesTheSymbolToTheFileHeader) {
  Message eth = MakeMessage();
  eth.symbol = "ETH/USD";

  EXPECT_THAT(SerializeToVector(eth),
              ElementsAreArray(SerializeToVector(MakeMessage())));
}

// Buys are written before sells, so swapping them changes the record.
TEST(MessageTest, DistinguishesBuysFromSells) {
  Message swapped = MakeMessage();
  std::swap(swapped.buys, swapped.sells);
  const std::vector<std::byte> bytes = SerializeToVector(swapped);

  EXPECT_THAT(bytes, Not(ElementsAreArray(SerializeToVector(MakeMessage()))));

  ByteReader reader{bytes};
  reader.SkipPrefix();
  EXPECT_THAT(reader.ReadSide(), ElementsAre(FieldsAre(8488880, 1)));
  EXPECT_THAT(reader.ReadSide(), ElementsAre(FieldsAre(8488870, 6980997),
                                             FieldsAre(8488860, 100000000)));
}

}  // namespace
}  // namespace l2
