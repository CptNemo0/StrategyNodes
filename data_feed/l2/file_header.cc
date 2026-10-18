#include "l2/file_header.h"

#include <algorithm>
#include <string_view>

#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"

namespace l2 {

namespace {

FileHeader::Text ToFixedText(std::string_view value) {
  FileHeader::Text text{};
  std::ranges::copy_n(value.begin(),
                      static_cast<std::ptrdiff_t>(
                          std::min(value.size(), FileHeader::kTextSize)),
                      text.begin());
  return text;
}

}  // namespace

FileHeader MakeFileHeader(std::string_view venue,
                          const BookSpec& book,
                          const PairPrecision& precision,
                          i64 start_time_ns) {
  return FileHeader{.magic = FileHeader::kMagic,
                    .format_version = FileHeader::kFormatVersion,
                    .depth = book.depth,
                    .price_decimals = precision.price_decimals,
                    .qty_decimals = precision.qty_decimals,
                    .start_time_ns = start_time_ns,
                    .venue = ToFixedText(venue),
                    .symbol = ToFixedText(book.symbol)};
}

}  // namespace l2
