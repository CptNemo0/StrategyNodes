#include "l2/book.h"

#include <iterator>
#include <span>

#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/message.h"

namespace l2 {

namespace {

template <typename Side>
void ApplyLevels(std::span<const Message::Level> levels,
                 u32 depth,
                 Side& side) {
  for (const Message::Level& level : levels) {
    if (level.quantity == 0) {
      side.erase(level.price);
    } else {
      side.insert_or_assign(level.price, level.quantity);
    }
  }
  if (depth != kFullDepth && side.size() > depth) {
    side.erase(std::next(side.begin(), depth), side.end());
  }
}

}  // namespace

void Book::Apply(const Message& message) {
  if (message.type == Message::Type::kSnapshot) {
    Clear();
  }
  ApplyLevels(message.buys, depth_, bids_);
  ApplyLevels(message.sells, depth_, asks_);
}

void Book::Clear() {
  bids_.clear();
  asks_.clear();
}

}  // namespace l2
