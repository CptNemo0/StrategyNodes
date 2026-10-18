#ifndef DATA_FEED_KRAKEN_PAIR_PRECISION_H_
#define DATA_FEED_KRAKEN_PAIR_PRECISION_H_

#include <span>
#include <string>

#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace kraken {

// Thrown when Kraken does not list a requested symbol, so a caller can tell a
// mistyped symbol from a venue that cannot be reached.
class UnknownPairError : public venue::UnknownSymbolError {
 public:
  using venue::UnknownSymbolError::UnknownSymbolError;
};

// Queries Kraken's public AssetPairs endpoint for `symbols` (e.g. "BTC/USD")
// and returns each one's price/quantity decimal precision, as reported by
// pair_decimals/lot_decimals. Throws UnknownPairError if Kraken does not list
// one of them, and std::runtime_error for any other failure.
l2::PairPrecisionMap FetchPairPrecision(std::span<const std::string> symbols);

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_PAIR_PRECISION_H_
