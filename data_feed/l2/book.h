#ifndef DATA_FEED_L2_BOOK_H_
#define DATA_FEED_L2_BOOK_H_

#include <functional>
#include <map>

#include "core/aliasing.h"
#include "l2/message.h"

namespace l2 {

// A pair's book at one depth, as its Messages leave it. Prices and
// quantities are the Messages' fixed-point integers.
class Book {
 public:
  // Price to quantity, best price first.
  using Bids = std::map<i64, i64, std::greater<>>;
  using Asks = std::map<i64, i64>;

  explicit Book(u32 depth) : depth_{depth} {}

  // A snapshot replaces the book; an update sets each level it carries, where
  // a zero quantity removes the level. Either way, whatever lies beyond the
  // depth is then dropped: a venue maintaining a book at that depth sends the
  // levels that move into it, but not the removal of those pushed out.
  void Apply(const Message& message);

  void Clear();

  const Bids& bids() const { return bids_; }
  const Asks& asks() const { return asks_; }

 private:
  u32 depth_;
  Bids bids_;
  Asks asks_;
};

}  // namespace l2

#endif  // ! DATA_FEED_L2_BOOK_H_
