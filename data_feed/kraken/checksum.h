#ifndef DATA_FEED_KRAKEN_CHECKSUM_H_
#define DATA_FEED_KRAKEN_CHECKSUM_H_

#include "core/aliasing.h"
#include "l2/book.h"

namespace kraken {

// How many levels of each side Kraken's book checksum covers.
inline constexpr int kChecksumDepth = 10;

// Kraken's CRC32 over the top kChecksumDepth asks, best first, then the top
// kChecksumDepth bids, best first, each level as its price then its quantity
// with the decimal point and leading zeros removed. The book's fixed-point
// integers already are those digits, provided they are scaled at the pair's
// precision on the feed. See
// https://docs.kraken.com/api/docs/guides/spot-ws-book-v2.
u32 BookChecksum(const l2::Book& book);

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_CHECKSUM_H_
