#ifndef DATA_FEED_ORDERBOOK_L2_H_
#define DATA_FEED_ORDERBOOK_L2_H_

#include <flat_map>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "aliasing.h"

namespace data_feed {

enum class OrderbookSide { kBuy = 0, kSell = 1 };

struct Level2Record {
  i64 price{};
  i64 quantity{};
  std::string to_string() const {
    return std::format("{:>13.8f} @ {:>7.1f}", quantity / 100000000.0,
                       price / 10.0);
  }

  double price_double() const { return static_cast<double>(price); }

  double quantity_double() const { return static_cast<double>(quantity); }
};

using Level2Records = std::vector<Level2Record>;

class OrderbookL2 {
 public:
  using Side = std::flat_map<i64, Level2Record, std::greater<>>;

  explicit OrderbookL2(u64 depth) : depth_(depth) {}

  const Level2Records& buy_side() const { return buy_side_.values(); }

  const Level2Records& sell_side() const { return sell_side_.values(); }

  i64 bid_price() const {
    return buy_side_.size() > 0 ? buy_side_.begin()->second.price : 0;
  }

  i64 ask_price() const {
    return buy_side_.size() > 0 ? buy_side_.rbegin()->second.price
                                : std::numeric_limits<i64>::max();
  }

  void UpdatePriceLevel(const Level2Record& record, OrderbookSide side);

  u32 CalculateChecksum() const;

  void Log() const;

 private:
  Side& GetSideRecords(OrderbookSide side) {
    if (side == OrderbookSide::kBuy) {
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

#endif  // ! DATA_FEED_ORDERBOOK_L2_H_
