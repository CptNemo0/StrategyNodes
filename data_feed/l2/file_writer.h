#ifndef DATA_FEED_L2_FILE_WRITER_H_
#define DATA_FEED_L2_FILE_WRITER_H_

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "l2/book_spec.h"
#include "l2/file_header.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace l2 {

// Records the Messages of a single pair on its own thread. Receive() queues
// a message; the thread drains the queue into
// `[symbol]-[start unix time].bin`, which is renamed to
// `[symbol]-[start unix time]-[end unix time].bin` once closed. The '/' of
// the pair is dropped from the file name, e.g. "BTCUSD". A final name already
// taken gets `-1`, `-2`, ... appended, so no recording replaces another.
//
// Each file opens with a FileHeader carrying the depth and precision, which is
// the only record of what the integers in the messages mean.
//
// Each unbroken stretch of the stream gets a file of its own, as a book cannot
// be carried across a gap (a dropped connection, or a resubscribe after a
// checksum mismatch): OnGap() closes the file, and the next message, the
// snapshot that ends the gap, opens a new one with a new start time. A file
// that has no message yet is kept for the next stretch, so failed reconnect
// attempts leave no empty files behind.
//
// A write failure only marks this writer failed() -- every other pair's
// writer keeps running, and messages received afterwards are dropped. Must not
// move, as the thread refers to this object.
class FileWriter : public core::Receiver<Message> {
 public:
  // Capacity of the SPSC queue between the parser and the writer thread.
  static constexpr std::size_t kMaxQueuedMessages = 256;

  // A message, or nullopt where a venue connection ended.
  using Entry = std::optional<Message>;

  using MessageQueue = boost::lockfree::
      spsc_queue<Entry, boost::lockfree::capacity<kMaxQueuedMessages>>;

  // `venue` (e.g. "kraken") and the rest are recorded in each file header.
  // Opens the first file at once, throwing if it cannot.
  FileWriter(std::string_view venue,
             const BookSpec& book,
             const PairPrecision& precision);

  FileWriter(const FileWriter&) = delete;
  FileWriter& operator=(const FileWriter&) = delete;

  // Drains the queue, then closes and renames the file.
  ~FileWriter() override;

  // Queues the message for the writer thread, waiting while the queue is
  // full. Only one thread at a time may call it or OnGap().
  void Receive(Message message) override;

  // Queues the end of the current file: once the messages queued before it
  // are written, it is closed and renamed. The same for every kind of gap, as
  // either way the book cannot be carried across it.
  void OnGap(core::Gap gap) override;

  // Set once the writer thread hits a fatal error.
  const std::atomic<bool>& failed() const { return failed_; }

 private:
  // Writer thread body for one queue entry. Throws on a failed write, which
  // the thread turns into failed().
  void Consume(const Entry& entry);

  // Opens `[symbol]-[now].bin` and writes its header. Throws if it cannot.
  void Open();

  // Flushed per message so nothing is lost when the process is killed. Throws
  // if the stream goes bad.
  void Write(const Message& message);

  // Closes the file and gives it its final name. Idempotent.
  void Close();

  MessageQueue queue_;
  // The pair without its '/', which every file name starts with.
  const std::string symbol_stem_;
  // Every file's header; only the start time differs between them.
  FileHeader header_;
  // The open file's name without ".bin", and its path.
  std::string file_stem_;
  std::filesystem::path path_;
  std::ofstream output_;
  // Written to the open file so far.
  u64 messages_in_file_{0};
  std::vector<std::byte> buffer_;
  std::atomic<bool> failed_{false};
  // Started at the end of the constructor, once the header is written, and
  // joined at the start of the destructor, before the file is closed.
  std::jthread thread_;
};

}  // namespace l2

#endif  // ! DATA_FEED_L2_FILE_WRITER_H_
