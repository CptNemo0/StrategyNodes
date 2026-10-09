#ifndef DATA_FEED_L2_MESSAGE_DISPATCHER_H_
#define DATA_FEED_L2_MESSAGE_DISPATCHER_H_

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <flat_map>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

#include "aliasing.h"
#include "constants.h"
#include "kraken_pair_precision.h"
#include "l2_message_writer_lane.h"
#include "pipeline_status.h"

namespace data_feed {

// A frame exactly as it came off the socket, paired with the moment it was
// read. The capture time travels with the frame because it must be taken on
// the network thread -- stamping it after the queue hop below would fold this
// queue's wait into the measurement.
struct RawFrame {
  std::string json;
  i64 capture_time_ns{0};
};

// Raw frames handed from the feed thread to the parser thread.
using FrameQueue =
    boost::lockfree::spsc_queue<RawFrame,
                                boost::lockfree::capacity<kMaxQueuedFrames>>;

using WriterLaneMap = std::
    flat_map<std::string, std::unique_ptr<L2MessageWriterLane>, std::less<>>;

// Parser thread body: parses frames into L2Messages and routes each to the
// lane of its symbol until stop is requested and `frames` is empty.
void ParseAndDispatch(const std::stop_token& stop,
                      FrameQueue& frames,
                      const WriterLaneMap& lanes,
                      const PairPrecisionMap& precision,
                      PipelineStatus& status);

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_DISPATCHER_H_
