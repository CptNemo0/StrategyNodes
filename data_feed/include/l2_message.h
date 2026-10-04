#ifndef DATA_FEED_L2_MESSAGE_H_
#define DATA_FEED_L2_MESSAGE_H_

#include <cstddef>
#include <span>
#include <vector>

#include "aliasing.h"

namespace data_feed {

struct L2Message {
  enum class Type : u32 { kUpdate, kSnapshot };

  struct Level {
    i64 price;
    i64 quantity;
  };

  // Serialized layout, native byte order:
  //   [u32 type][u64 buys bytes][buys...][u64 sells bytes][sells...]
  // where each side's byte count is size() * sizeof(Level).
  constexpr u64 GetByteSize() const {
    return sizeof(Type) + sizeof(u64) + buys.size() * sizeof(Level) +
           sizeof(u64) + sells.size() * sizeof(Level);
  }

  // Writes the layout above into the front of out, which must hold at least
  // GetByteSize() bytes. Returns the number of bytes written.
  u64 Serialize(std::span<std::byte> out) const;

  Type type;
  std::vector<Level> buys;
  std::vector<Level> sells;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_H_
