#ifndef DATA_FEED_L2_PAIR_PRECISION_H_
#define DATA_FEED_L2_PAIR_PRECISION_H_

#include <flat_map>
#include <functional>
#include <string>

#include "core/aliasing.h"

namespace l2 {

// Decimal digit counts a venue applies to a pair's price and quantity ticks.
// Prices and quantities in a Message are integers scaled by 10^decimals.
struct PairPrecision {
  u32 price_decimals;
  u32 qty_decimals;
};

using PairPrecisionMap = std::flat_map<std::string, PairPrecision, std::less<>>;

}  // namespace l2

#endif  // ! DATA_FEED_L2_PAIR_PRECISION_H_
