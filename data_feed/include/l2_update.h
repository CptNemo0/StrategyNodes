#ifndef DATA_FEED_L2_UPDATE
#define DATA_FEED_L2_UPDATE

#include "aliasing.h"
#include "l2_record.h"

namespace data_feed {

struct Level2Update {
  Level2Records bids;
  Level2Records asks;
  u32 checksum{};
};

}  // namespace data_feed

#endif  // !DATA_FEED_L2_UPDATE
