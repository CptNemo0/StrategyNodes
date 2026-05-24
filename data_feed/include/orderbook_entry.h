#ifndef DATA_FEED_ORDERBOOK_ENTRY_H_
#define DATA_FEED_ORDERBOOK_ENTRY_H_

#include "aliasing.h"
#include <print>

struct OrderbookEntry {
  f64 price;
  f64 size;
  u32 quantity;

  // Both parsers route identical char ranges through std::from_chars (which is
  // deterministic), so the doubles produced are bitwise-equal — safe to ==.
  bool operator==(const OrderbookEntry &) const = default;

  void Log() const {
    std::println("price: {} | size: {} | quantity: {}", price, size, quantity);
  }
};

#endif //! DATA_FEED_ORDERBOOK_ENTRY_H_
