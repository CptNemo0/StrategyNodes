#ifndef DATA_FEED_NET_RAW_FRAME_H_
#define DATA_FEED_NET_RAW_FRAME_H_

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <string>

#include "core/aliasing.h"

namespace net {

// Capacity of the SPSC queue between the feed and the parser.
inline constexpr std::size_t kMaxQueuedFrames = 256;

// A frame exactly as it came off the socket, paired with the moment it was
// read. The capture time travels with the frame because it must be taken on
// the network thread -- stamping it after the queue hop below would fold this
// queue's wait into the measurement.
struct RawFrame {
  std::string json;
  i64 capture_time_ns{0};
};

// Raw frames handed from the network thread to the parser thread.
using RawFrameQueue =
    boost::lockfree::spsc_queue<RawFrame,
                                boost::lockfree::capacity<kMaxQueuedFrames>>;

}  // namespace net

#endif  // ! DATA_FEED_NET_RAW_FRAME_H_
