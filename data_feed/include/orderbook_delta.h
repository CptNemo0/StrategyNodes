#ifndef DATA_FEED_ORDERBOOK_DELTA_H_
#define DATA_FEED_ORDERBOOK_DELTA_H_

#include <cstddef>
#include <print>
#include <string>
#include <vector>

#include "aliasing.h"
#include "orderbook_entry.h"

struct OrderbookDelta {
  alignas(64) std::vector<OrderbookEntry> asks_;
  alignas(64) std::vector<OrderbookEntry> bids_;
  alignas(64) u64 sequence;

  bool operator==(const OrderbookDelta &) const = default;

  void Log(i64 head = -1) const {
    std::println("Sequence: {}", sequence);

    std::println("Bids ({})", head < 0 ? "full" : std::to_string(head));

    for (auto i{0uz};
         i < (head > 0 ? std::min(bids_.size(), static_cast<u64>(head))
                       : bids_.size());
         ++i) {
      bids_[i].Log();
    }

    std::println("Asks ({})", head < 0 ? "full" : std::to_string(head));
    for (auto i{0uz};
         i < (head > 0 ? std::min(asks_.size(), static_cast<u64>(head))
                       : bids_.size());
         ++i) {
      asks_[i].Log();
    }
  }
};

#endif //! DATA_FEED_ORDERBOOK_DELTA_H_
