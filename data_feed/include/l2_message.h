#ifndef DATA_FEED_L2_MESSAGE_H_
#define DATA_FEED_L2_MESSAGE_H_

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "aliasing.h"

namespace data_feed {

struct L2Message {
  enum class Type : u32 { kUpdate, kSnapshot };

  struct Level {
    // Fixed-point, scaled by 10^decimals (see PairPrecision) so the exact
    // decimal text Kraken sent round-trips without floating-point error.
    i64 price;
    i64 quantity;
  };

  // Serialized layout, native byte order:
  //   [u32 type][u32 checksum][i64 venue time][i64 capture time]
  //   [u64 buys bytes][buys...][u64 sells bytes][sells...]
  // where each side's byte count is size() * sizeof(Level). The 24 byte
  // prefix keeps both level arrays 8 byte aligned, so a reader can map them
  // in place.
  constexpr u64 GetByteSize() const {
    return sizeof(Type) + sizeof(checksum) + sizeof(venue_time_ns) +
           sizeof(capture_time_ns) + sizeof(u64) + buys.size() * sizeof(Level) +
           sizeof(u64) + sells.size() * sizeof(Level);
  }

  // Writes the layout above into the front of out, which must hold at least
  // GetByteSize() bytes. Returns the number of bytes written.
  u64 Serialize(std::span<std::byte> out) const;

  Type type;
  // Kraken's CRC32 over the book state this message leaves behind, so a
  // replay can prove it rebuilt the same book. Zero if the venue omitted it.
  u32 checksum;
  // The venue's own timestamp, nanoseconds since the Unix epoch. Zero if the
  // venue omitted it or sent it in an unexpected layout.
  i64 venue_time_ns;
  // When the recorder read the frame off the socket, nanoseconds since the
  // Unix epoch. Taken on the network thread, so it is not skewed by the wait
  // in the queue to the parser. Subtracting the venue time gives the
  // transport delay.
  i64 capture_time_ns;
  // Pair the message belongs to, e.g. "BTC/USD". Not serialized; it is in the
  // file header, and a recording holds exactly one pair.
  std::string symbol;
  std::vector<Level> buys;
  std::vector<Level> sells;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_H_
