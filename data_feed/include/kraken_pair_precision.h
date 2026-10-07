#ifndef DATA_FEED_KRAKEN_PAIR_PRECISION_H_
#define DATA_FEED_KRAKEN_PAIR_PRECISION_H_

#include <flat_map>
#include <functional>
#include <span>
#include <string>

#include "aliasing.h"

namespace data_feed {

// Decimal digit counts Kraken applies to a pair's price and quantity ticks,
// as reported by the AssetPairs REST endpoint's pair_decimals/lot_decimals.
struct PairPrecision {
  u32 price_decimals;
  u32 qty_decimals;
};

using PairPrecisionMap = std::flat_map<std::string, PairPrecision, std::less<>>;

// Queries Kraken's public AssetPairs endpoint for `symbols` (e.g. "BTC/USD")
// and returns each one's price/quantity decimal precision. Throws
// std::runtime_error if the venue reports an error or omits a requested
// symbol.
PairPrecisionMap FetchKrakenPairPrecision(std::span<const std::string> symbols);

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_PAIR_PRECISION_H_
