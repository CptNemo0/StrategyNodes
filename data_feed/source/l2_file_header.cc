#include "l2_file_header.h"

#include <algorithm>
#include <string_view>

#include "aliasing.h"
#include "kraken_pair_precision.h"

namespace data_feed {

namespace {

L2FileHeader::Text ToFixedText(std::string_view value) {
  L2FileHeader::Text text{};
  std::ranges::copy_n(value.begin(),
                      static_cast<std::ptrdiff_t>(
                          std::min(value.size(), L2FileHeader::kTextSize)),
                      text.begin());
  return text;
}

}  // namespace

L2FileHeader MakeL2FileHeader(std::string_view venue,
                              std::string_view symbol,
                              u32 depth,
                              const PairPrecision& precision,
                              i64 start_time_ns) {
  return L2FileHeader{.magic = L2FileHeader::kMagic,
                      .format_version = L2FileHeader::kFormatVersion,
                      .depth = depth,
                      .price_decimals = precision.price_decimals,
                      .qty_decimals = precision.qty_decimals,
                      .start_time_ns = start_time_ns,
                      .venue = ToFixedText(venue),
                      .symbol = ToFixedText(symbol)};
}

}  // namespace data_feed
