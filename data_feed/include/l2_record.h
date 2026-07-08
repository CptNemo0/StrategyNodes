#ifndef DATA_FEED_L2_RECORD
#define DATA_FEED_L2_RECORD

#include <format>
#include <string>
#include <vector>

#include "aliasing.h"

namespace data_feed {

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

}  // namespace data_feed

#endif  // !DATA_FEED_L2_RECORD
