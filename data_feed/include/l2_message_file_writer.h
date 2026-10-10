#ifndef DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
#define DATA_FEED_L2_MESSAGE_FILE_WRITER_H_

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aliasing.h"
#include "constants.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "receiver.h"

namespace data_feed {

// Records the L2Messages of a single pair on its own thread. Receive() queues
// a message; the thread drains the queue into
// `[symbol]-[start unix time].bin`, which is renamed to
// `[symbol]-[start unix time]-[end unix time].bin` on destruction. The '/' of
// the pair is dropped from the file name, e.g. "BTCUSD".
//
// The file opens with an L2FileHeader carrying the depth and precision, which
// is the only record of what the integers in the messages mean.
//
// A write failure only marks this writer failed() -- every other pair's
// writer keeps running, and messages received afterwards are dropped. Must not
// move, as the thread refers to this object.
class L2MessageFileWriter : public Receiver<L2Message> {
 public:
  using MessageQueue = boost::lockfree::
      spsc_queue<L2Message, boost::lockfree::capacity<kMaxQueuedMessages>>;

  // `venue` (e.g. "kraken") and the rest are recorded in the file header.
  L2MessageFileWriter(std::string_view venue,
                      const std::string& symbol,
                      u32 depth,
                      const PairPrecision& precision);

  L2MessageFileWriter(const L2MessageFileWriter&) = delete;
  L2MessageFileWriter& operator=(const L2MessageFileWriter&) = delete;

  // Drains the queue, then closes and renames the file.
  ~L2MessageFileWriter() override;

  // Queues the message for the writer thread, waiting while the queue is
  // full. Only one thread may call it.
  void Receive(L2Message message) override;

  // Set once the writer thread hits a fatal error.
  const std::atomic<bool>& failed() const { return failed_; }

 private:
  // Flushed per message so nothing is lost when the process is killed. Throws
  // if the stream goes bad, which the thread turns into failed().
  void Write(const L2Message& message);

  // Closes the file and gives it its final name. Idempotent.
  void Close();

  MessageQueue queue_;
  // Declared before the file name: the name and the header both derive from
  // it, so they agree on when the recording started.
  i64 start_time_ns_;
  std::string file_stem_;
  std::filesystem::path path_;
  std::ofstream output_;
  std::vector<std::byte> buffer_;
  std::atomic<bool> failed_{false};
  // Started at the end of the constructor, once the header is written, and
  // joined at the start of the destructor, before the file is closed.
  std::jthread thread_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
