#ifndef DATA_FEED_KRAKEN_TRADABLE_PAIRS_H_
#define DATA_FEED_KRAKEN_TRADABLE_PAIRS_H_

#include <string>
#include <vector>

namespace data_feed {

// Queries Kraken's public AssetPairs endpoint for every pair and returns the
// symbols (e.g. "BTC/USD") whose status is "online". Throws
// std::runtime_error if the venue reports an error.
std::vector<std::string> FetchKrakenTradablePairs();

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_TRADABLE_PAIRS_H_
