#ifndef DATA_FEED_KRAKEN_PAIR_PRECISION_H_
#define DATA_FEED_KRAKEN_PAIR_PRECISION_H_

#include <span>
#include <string>

#include "l2/pair_precision.h"

namespace kraken {

// Queries Kraken's public AssetPairs endpoint for `symbols` (e.g. "BTC/USD")
// and returns each one's price/quantity decimal precision, as reported by
// pair_decimals/lot_decimals. Throws std::runtime_error if the venue reports
// an error or omits a requested symbol.
l2::PairPrecisionMap FetchPairPrecision(std::span<const std::string> symbols);

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_PAIR_PRECISION_H_
