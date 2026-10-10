#include "crypto/hash.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Not;
using ::testing::SizeIs;

std::vector<unsigned char> Bytes(std::string_view text) {
  return text | std::ranges::to<std::vector<unsigned char>>();
}

std::string ToHex(std::span<const unsigned char> bytes) {
  return bytes | std::views::transform([](unsigned char byte) {
           return std::format("{:02x}", byte);
         }) |
         std::views::join | std::ranges::to<std::string>();
}

// SHA-256 matches the FIPS 180 reference digest.
TEST(Sha256Test, MatchesTheFips180Vector) {
  EXPECT_EQ(ToHex(Sha256("abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// SHA-256 of empty input matches the reference digest.
TEST(Sha256Test, DigestsEmptyInput) {
  EXPECT_EQ(ToHex(Sha256("")),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// Input is hashed by its length, not up to the first null byte.
TEST(Sha256Test, HashesEmbeddedNullBytes) {
  constexpr std::string_view kWithNull{"a\0b", 3};
  EXPECT_THAT(Sha256(kWithNull), Not(ElementsAreArray(Sha256("a"))));
  EXPECT_THAT(Sha256(kWithNull),
              Not(ElementsAreArray(Sha256(std::string_view{"a\0c", 3}))));
}

// HMAC-SHA512 matches the RFC 4231 reference, as Kraken's API signing
// requires.
TEST(HmacSha512Test, MatchesRfc4231TestCase2) {
  EXPECT_EQ(
      ToHex(HmacSha512(Bytes("Jefe"), Bytes("what do ya want for nothing?"))),
      "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
      "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
}

// Keys longer than the block size are handled as RFC 4231 specifies.
TEST(HmacSha512Test, HashesAKeyLongerThanTheBlockSize) {
  // RFC 4231 test case 6: a 131 byte key is hashed down before use.
  EXPECT_EQ(
      ToHex(HmacSha512(
          std::vector<unsigned char>(131, 0xAA),
          Bytes("Test Using Larger Than Block-Size Key - Hash Key First"))),
      "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
      "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598");
}

// HMAC-SHA512 produces exactly 64 bytes.
TEST(HmacSha512Test, ProducesA64ByteDigest) {
  EXPECT_THAT(HmacSha512(Bytes("key"), Bytes("message")), SizeIs(64));
}

// Different keys sign the same message differently.
TEST(HmacSha512Test, DependsOnTheKey) {
  EXPECT_THAT(
      HmacSha512(Bytes("key one"), Bytes("message")),
      Not(ElementsAreArray(HmacSha512(Bytes("key two"), Bytes("message")))));
}

}  // namespace
}  // namespace crypto
