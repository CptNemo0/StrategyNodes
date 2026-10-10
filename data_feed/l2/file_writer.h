#ifndef DATA_FEED_L2_FILE_WRITER_H_
#define DATA_FEED_L2_FILE_WRITER_H_

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

#include "core/aliasing.h"
#include "core/receiver.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace l2 {

// Records the Messages of a single pair on its own thread. Receive() queues
// a message; the thread drains the queue into
// `[symbol]-[start unix time].bin`, which is renamed to
// `[symbol]-[start unix time]-[end unix time].bin` on destruction. The '/' of
// the pair is dropped from the file name, e.g. "BTCUSD".
//
// The file opens with a FileHeader carrying the depth and precision, which is
// the only record of what the integers in the messages mean.
//
// A write failure only marks this writer failed() -- every other pair's
// writer keeps running, and messages received afterwards are dropped. Must not
// move, as the thread refers to this object.
class FileWriter : public core::Receiver<Message> {
 public:
  // Capacity of the SPSC queue between the parser and the writer thread.
  static constexpr std::size_t kMaxQueuedMessages = 256;

  using MessageQueue = boost::lockfree::
      spsc_queue<Message, boost::lockfree::capacity<kMaxQueuedMessages>>;

  // `venue` (e.g. "kraken") and the rest are recorded in the file header.
  FileWriter(std::string_view venue,
             const std::string& symbol,
             u32 depth,
             const PairPrecision& precision);

  FileWriter(const FileWriter&) = delete;
  FileWriter& operator=(const FileWriter&) = delete;

  // Drains the queue, then closes and renames the file.
  ~FileWriter() override;

  // Queues the message for the writer thread, waiting while the queue is
  // full. Only one thread may call it.
  void Receive(Message message) override;

  // Set once the writer thread hits a fatal error.
  const std::atomic<bool>& failed() const { return failed_; }

 private:
  // Flushed per message so nothing is lost when the process is killed. Throws
  // if the stream goes bad, which the thread turns into failed().
  void Write(const Message& message);

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

}  // namespace l2

#endif  // ! DATA_FEED_L2_FILE_WRITER_H_
