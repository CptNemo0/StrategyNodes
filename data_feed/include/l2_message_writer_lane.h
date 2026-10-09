#ifndef DATA_FEED_L2_MESSAGE_WRITER_LANE_H_
#define DATA_FEED_L2_MESSAGE_WRITER_LANE_H_

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <string>
#include <thread>

#include "aliasing.h"
#include "constants.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "l2_message_file_writer.h"

namespace data_feed {

// Writes the L2Messages of a single pair on its own thread. The parser pushes
// into queue(); the thread drains it into the file writer. A write failure
// only marks this lane failed() -- every other pair's lane keeps running.
// Must not move, as the thread refers to this object.
class L2MessageWriterLane {
 public:
  using MessageQueue = boost::lockfree::
      spsc_queue<L2Message, boost::lockfree::capacity<kMaxQueuedMessages>>;

  // `depth` and `precision` are recorded in the file header; see
  // L2MessageFileWriter.
  L2MessageWriterLane(const std::string& symbol,
                      u32 depth,
                      const PairPrecision& precision);

  L2MessageWriterLane(const L2MessageWriterLane&) = delete;
  L2MessageWriterLane& operator=(const L2MessageWriterLane&) = delete;

  // Producer end, for the parser thread only.
  MessageQueue& queue() { return queue_; }

  // Set once the writer thread hits a fatal error. The feed should
  // unsubscribe this pair; nothing in-process does so automatically.
  const std::atomic<bool>& failed() const { return failed_; }

 private:
  // Members are destroyed in reverse order, so the thread drains and joins
  // before the writer closes its file.
  MessageQueue queue_;
  L2MessageFileWriter writer_;
  std::atomic<bool> failed_{false};
  std::jthread thread_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_WRITER_LANE_H_
