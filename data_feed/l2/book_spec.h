#ifndef DATA_FEED_L2_BOOK_SPEC_H_
#define DATA_FEED_L2_BOOK_SPEC_H_

#include <string>

#include "core/aliasing.h"

namespace l2 {

// The depth of a book without a limit: what a venue that always streams the
// full book (e.g. Coinbase) is subscribed at. Zero, as it is what an unset
// depth reads as on the wire.
inline constexpr u32 kFullDepth = 0;

// A pair's book at one depth, e.g. "BTC/USD" at 10, or at kFullDepth: what a
// subscription streams.
struct BookSpec {
  std::string symbol;
  u32 depth;
};

}  // namespace l2

#endif  // ! DATA_FEED_L2_BOOK_SPEC_H_
