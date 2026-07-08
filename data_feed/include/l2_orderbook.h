#ifndef DATA_FEED_L2_ORDERBOOK_H_
#define DATA_FEED_L2_ORDERBOOK_H_

#include <flat_map>
#include <functional>
#include <limits>

#include "aliasing.h"
#include "l2_record.h"
#include "l2_update.h"
#include "orderbook_sides.h"

namespace data_feed {

class Level2Orderbook {
 public:
  using Side = std::flat_map<i64, Level2Record, std::greater<>>;

  explicit Level2Orderbook(u64 depth) : depth_(depth) {}

  const Level2Records& buy_side() const { return buy_side_.values(); }

  const Level2Records& sell_side() const { return sell_side_.values(); }

  i64 bid_price() const {
    return buy_side_.size() > 0 ? buy_side_.begin()->second.price : 0;
  }

  i64 ask_price() const {
    return sell_side_.size() > 0 ? sell_side_.rbegin()->second.price
                                 : std::numeric_limits<i64>::max();
  }

  const Level2Record& best_bid() const { return buy_side_.begin()->second; }

  const Level2Record& best_ask() const { return sell_side_.rbegin()->second; }

  void UpdatePriceLevel(const Level2Record& record, OrderbookSides side);

  void ConsumeUpdate(const Level2Update& update);

  u32 CalculateChecksum() const;

  void Log() const;

 private:
  Side& GetSideRecords(OrderbookSides side) {
    if (side == OrderbookSides::kBuy) {
      return buy_side_;
    } else {
      return sell_side_;
    }
  }

  u64 depth_;
  Side buy_side_;
  Side sell_side_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_ORDERBOOK_H_
