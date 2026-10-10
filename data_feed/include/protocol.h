#ifndef DATA_FEED_PROTOCOL_H_
#define DATA_FEED_PROTOCOL_H_

#include "aliasing.h"

namespace data_feed {

// Transport a venue streams its data over. Each one has its own DataFeed
// specialization.
enum class Protocol : u8 { kWebsocket };

}  // namespace data_feed

#endif  // ! DATA_FEED_PROTOCOL_H_
