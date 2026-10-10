#ifndef DATA_FEED_L2_FILE_HEADER_H_
#define DATA_FEED_L2_FILE_HEADER_H_

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "core/aliasing.h"
#include "l2/pair_precision.h"

namespace l2 {

// Fixed 64 byte preamble written once at the front of every .bin recording.
// It is what makes a recording self describing: the records that follow hold
// prices as integers scaled by 10^price_decimals, so without the precision
// they cannot be decoded at all, and re-reading it from the venue later would
// silently decode old files with new scales. The depth is likewise
// unrecoverable from the records, since an update only carries changed levels.
struct FileHeader {
  // "KRKNL2", zero padded.
  static constexpr std::array<char, 8> kMagic = {'K', 'R', 'K',  'N',
                                                 'L', '2', '\0', '\0'};

  // Bump on any change to this header or to the record layout behind it (see
  // Message's layout comment). Readers must refuse a version they predate.
  static constexpr u32 kFormatVersion = 1;

  // Fixed width text fields, zero padded; longer input is truncated. A value
  // that fills the field has no terminator, so a reader must bound it by
  // kTextSize rather than treating it as a C string.
  static constexpr std::size_t kTextSize = 16;
  using Text = std::array<char, kTextSize>;

  std::array<char, 8> magic;
  u32 format_version;
  // Book depth subscribed to, one the venue supports (e.g. kraken::kL2Depths).
  u32 depth;
  u32 price_decimals;
  u32 qty_decimals;
  // When recording started, nanoseconds since the Unix epoch.
  i64 start_time_ns;
  Text venue;
  Text symbol;
};

static_assert(std::is_trivially_copyable_v<FileHeader>);
static_assert(sizeof(FileHeader) == 64,
              "FileHeader is written as raw bytes, so its size is part of the "
              "file format");

// Builds the preamble for a recording of `symbol` at `depth`. `venue` and
// `symbol` are truncated to kTextSize characters.
FileHeader MakeFileHeader(std::string_view venue,
                          std::string_view symbol,
                          u32 depth,
                          const PairPrecision& precision,
                          i64 start_time_ns);

}  // namespace l2

#endif  // ! DATA_FEED_L2_FILE_HEADER_H_
