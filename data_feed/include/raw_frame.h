#ifndef DATA_FEED_RAW_FRAME_H_
#define DATA_FEED_RAW_FRAME_H_

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <string>

#include "aliasing.h"
#include "constants.h"

namespace data_feed {

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

}  // namespace data_feed

#endif  // ! DATA_FEED_RAW_FRAME_H_
