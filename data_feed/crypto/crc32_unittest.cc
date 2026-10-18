#include "crypto/crc32.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "core/aliasing.h"

namespace crypto {
namespace {

using ::testing::Message;

// Agrees with the standard CRC-32 that Kraken's book checksum uses.
TEST(Crc32Test, MatchesTheStandardCheckValue) {
  // The CRC-32/ISO-HDLC check value every conforming implementation agrees on.
  EXPECT_EQ(Crc32("123456789"), 0xCBF43926u);
}

// Empty input checksums to zero, as the standard defines.
TEST(Crc32Test, OfNothingIsZero) {
  EXPECT_EQ(Crc32(""), 0u);
}

// Agrees with reference digests for ordinary text.
TEST(Crc32Test, MatchesKnownTextDigests) {
  EXPECT_EQ(Crc32("a"), 0xE8B7BE43u);
  EXPECT_EQ(Crc32("The quick brown fox jumps over the lazy dog"), 0x414FA339u);
}

// Any single-bit corruption of the input changes the checksum.
TEST(Crc32Test, DetectsASingleFlippedBit) {
  std::string data = "BTC/USD 84888.7 0.06980997";
  const u32 original = Crc32(data);

  for (std::size_t i{0}; i < data.size(); ++i) {
    SCOPED_TRACE(Message() << "flipped byte " << i);
    data[i] ^= 0x01;
    EXPECT_NE(Crc32(data), original);
    data[i] ^= 0x01;
  }
}

// Bytes above 0x7F are hashed as unsigned values, not sign-extended.
TEST(Crc32Test, TreatsBytesAboveSevenBitsAsUnsigned) {
  // A signed char would sign-extend into the table index.
  EXPECT_EQ(Crc32(std::string_view{"\xFF", 1}), 0xFF000000u);
}

}  // namespace
}  // namespace crypto
