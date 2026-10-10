#ifndef DATA_FEED_DATA_LEVEL_H_
#define DATA_FEED_DATA_LEVEL_H_

#include "aliasing.h"

namespace data_feed {

// Granularity of the market data a parser produces.
enum class DataLevel : u8 { kL2, kL3 };

}  // namespace data_feed

#endif  // ! DATA_FEED_DATA_LEVEL_H_
