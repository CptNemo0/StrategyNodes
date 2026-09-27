#ifndef DATA_FEED_KRAKEN_MESSAGE_SIZE_UTIL_H_
#define DATA_FEED_KRAKEN_MESSAGE_SIZE_UTIL_H_

#include <array>
#include <cstddef>

#include "aliasing.h"

namespace data_feed {

// The only depths Kraken's L2 "book" channel accepts.
inline constexpr std::array<u64, 5> kKrakenL2Depths = {10, 25, 100, 500, 1000};

// I assume that the initial snapshot of the orderbook is the longest message
// that will ever be sent. Since the Kraken's L2 orderbook channel fetches only
// at few depths - `kKrakenL2Depths`, maximum lengths of messages at respective
// depths can be calculated at compile time. While this function is not
// constexpr it calls one that is.
std::size_t MaxL2SnapshotLength(u64 depth, std::size_t symbol_length);

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_MESSAGE_SIZE_UTIL_H_
