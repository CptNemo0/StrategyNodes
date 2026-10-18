#ifndef DATA_FEED_L2_BOOK_SPEC_H_
#define DATA_FEED_L2_BOOK_SPEC_H_

#include <string>

#include "core/aliasing.h"

namespace l2 {

// A pair's book at one depth, e.g. "BTC/USD" at 10: what a subscription
// streams.
struct BookSpec {
  std::string symbol;
  u32 depth;
};

}  // namespace l2

#endif  // ! DATA_FEED_L2_BOOK_SPEC_H_
