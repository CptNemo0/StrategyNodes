#include "crypto/base64.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {
namespace {

using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Message;
using ::testing::Not;
using ::testing::Truly;

std::vector<unsigned char> Bytes(std::string_view text) {
  return text | std::ranges::to<std::vector<unsigned char>>();
}

std::string Encode(std::string_view text) {
  return Base64Encode(Bytes(text));
}

// The test vectors of RFC 4648, section 10.
struct Rfc4648Vector {
  std::string_view decoded;
  std::string_view encoded;
};

constexpr Rfc4648Vector kRfc4648Vectors[] = {
    {.decoded = "f", .encoded = "Zg=="},
    {.decoded = "fo", .encoded = "Zm8="},
    {.decoded = "foo", .encoded = "Zm9v"},
    {.decoded = "foob", .encoded = "Zm9vYg=="},
    {.decoded = "fooba", .encoded = "Zm9vYmE="},
    {.decoded = "foobar", .encoded = "Zm9vYmFy"},
};

// Encoding matches the RFC 4648 vectors at every padding length.
TEST(Base64Test, EncodesTheRfc4648Vectors) {
  for (const Rfc4648Vector& vector : kRfc4648Vectors) {
    SCOPED_TRACE(Message() << '"' << vector.decoded << '"');
    EXPECT_EQ(Encode(vector.decoded), vector.encoded);
  }
}

// Decoding matches the RFC 4648 vectors, with the padding stripped from
// the output.
TEST(Base64Test, DecodesTheRfc4648Vectors) {
  for (const Rfc4648Vector& vector : kRfc4648Vectors) {
    SCOPED_TRACE(Message() << '"' << vector.encoded << '"');
    EXPECT_THAT(Base64Decode(vector.encoded),
                ElementsAreArray(Bytes(vector.decoded)));
  }
}

// Empty input stays empty both ways.
TEST(Base64Test, EncodesAndDecodesNothingAsNothing) {
  EXPECT_THAT(Base64Encode({}), IsEmpty());
  EXPECT_THAT(Base64Decode(""), IsEmpty());
}

// Any binary data, e.g. an HMAC key, survives an encode/decode round trip.
TEST(Base64Test, RoundTripsEveryByteValueAtEveryPaddingLength) {
  const std::vector<unsigned char> all_bytes =
      std::views::iota(0, 256) | std::views::transform([](int byte) {
        return static_cast<unsigned char>(byte);
      }) |
      std::ranges::to<std::vector>();

  // Lengths 254 to 256 cover all three remainders mod 3, i.e. "==", "=" and
  // no padding.
  for (const std::size_t length : {254uz, 255uz, 256uz}) {
    SCOPED_TRACE(Message() << "length " << length);
    const std::span<const unsigned char> data{all_bytes.data(), length};
    EXPECT_THAT(Base64Decode(Base64Encode(data)), ElementsAreArray(data));
  }
}

// Whitespace and line breaks inside the text are ignored.
TEST(Base64Test, DecodingSkipsLineBreaksAndOtherNoise) {
  EXPECT_THAT(Base64Decode("Zm9v\nYmFy\r\n"),
              ElementsAreArray(Bytes("foobar")));
  EXPECT_THAT(Base64Decode("  Zm9v YmE="), ElementsAreArray(Bytes("fooba")));
}

// Noise after the padding does not hide it, so no zero bytes are added.
TEST(Base64Test, DecodingSkipsNoiseAfterThePadding) {
  // A secret pasted into an environment variable often ends in a newline.
  EXPECT_THAT(Base64Decode("Zm9vYmE=\n"), ElementsAreArray(Bytes("fooba")));
  EXPECT_THAT(Base64Decode("Zg==\r\n"), ElementsAreArray(Bytes("f")));
}

// Text that does not form whole base64 groups is rejected.
TEST(Base64Test, DecodingRejectsATruncatedInput) {
  EXPECT_THROW(Base64Decode("Zm9vY"), std::runtime_error);
  EXPECT_THROW(Base64Decode("Zm9"), std::runtime_error);
}

// Exactly the base64 alphabet and padding count as base64 characters.
TEST(Base64Test, ClassifiesTheBase64Alphabet) {
  EXPECT_THAT(
      std::string{
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="},
      Each(Truly(IsValidBase64Character)));
  EXPECT_THAT(std::string{" \n\r\t-_.,:\"\\"},
              Each(Not(Truly(IsValidBase64Character))));
}

}  // namespace
}  // namespace crypto
