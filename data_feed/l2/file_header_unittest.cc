#include "l2/file_header.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"

namespace l2 {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Field;

constexpr PairPrecision kBtcPrecision{.price_decimals = 1, .qty_decimals = 8};
constexpr i64 kStartTimeNs = 1'790'508'579'799'080'000;

FileHeader MakeHeader(std::string_view venue, std::string_view symbol) {
  return MakeFileHeader(venue, {.symbol = std::string{symbol}, .depth = 100},
                        kBtcPrecision, kStartTimeNs);
}

// `text` as a fixed width field: zero padded, or cut at kTextSize.
FileHeader::Text Padded(std::string_view text) {
  FileHeader::Text field{};
  std::ranges::copy(text.substr(0, FileHeader::kTextSize), field.begin());
  return field;
}

// Every header identifies the file format and its version.
TEST(FileHeaderTest, StartsWithTheMagicAndCurrentVersion) {
  const FileHeader header = MakeHeader("kraken", "BTC/USD");

  EXPECT_THAT(header.magic,
              ElementsAreArray({'K', 'R', 'K', 'N', 'L', '2', '\0', '\0'}));
  EXPECT_EQ(header.format_version, FileHeader::kFormatVersion);
}

// The header records the depth, precision and start time a reader needs
// to decode the records.
TEST(FileHeaderTest, RecordsWhatIsNeededToDecodeTheRecords) {
  EXPECT_THAT(
      MakeHeader("kraken", "BTC/USD"),
      AllOf(Field("depth", &FileHeader::depth, 100u),
            Field("price_decimals", &FileHeader::price_decimals,
                  kBtcPrecision.price_decimals),
            Field("qty_decimals", &FileHeader::qty_decimals,
                  kBtcPrecision.qty_decimals),
            Field("start_time_ns", &FileHeader::start_time_ns, kStartTimeNs)));
}

// Short venue and symbol text is zero padded to the field width.
TEST(FileHeaderTest, ZeroPadsShortText) {
  EXPECT_THAT(MakeHeader("kraken", "BTC/USD"),
              AllOf(Field("venue", &FileHeader::venue,
                          ElementsAreArray({'k', 'r', 'a', 'k', 'e', 'n', '\0',
                                            '\0', '\0', '\0', '\0', '\0', '\0',
                                            '\0', '\0', '\0'})),
                    Field("symbol", &FileHeader::symbol,
                          ElementsAreArray(Padded("BTC/USD")))));
}

// Text longer than a field is cut at its width.
TEST(FileHeaderTest, TruncatesTextLongerThanTheField) {
  EXPECT_THAT(MakeHeader("a venue with a long name", "LONGASSET/OTHERASSET"),
              AllOf(Field("venue", &FileHeader::venue,
                          ElementsAreArray(Padded("a venue with a l"))),
                    Field("symbol", &FileHeader::symbol,
                          ElementsAreArray(Padded("LONGASSET/OTHERA")))));
}

// Text exactly as wide as a field fills it, with no terminator.
TEST(FileHeaderTest, FillsAFieldExactlyWithoutATerminator) {
  EXPECT_THAT(MakeHeader("kraken", "EXACTLY16CHARS!!").symbol,
              ElementsAreArray(Padded("EXACTLY16CHARS!!")));
}

// The header survives being written and read back as raw bytes.
TEST(FileHeaderTest, SurvivesARawByteRoundTrip) {
  const FileHeader written = MakeHeader("kraken", "ETH/USD");
  std::array<char, sizeof(FileHeader)> bytes{};
  std::memcpy(bytes.data(), &written, sizeof(written));

  FileHeader read{};
  std::memcpy(&read, bytes.data(), sizeof(read));

  EXPECT_THAT(read, AllOf(Field("magic", &FileHeader::magic,
                                ElementsAreArray(FileHeader::kMagic)),
                          Field("depth", &FileHeader::depth, written.depth),
                          Field("start_time_ns", &FileHeader::start_time_ns,
                                written.start_time_ns),
                          Field("symbol", &FileHeader::symbol,
                                ElementsAreArray(Padded("ETH/USD")))));
}

}  // namespace
}  // namespace l2
