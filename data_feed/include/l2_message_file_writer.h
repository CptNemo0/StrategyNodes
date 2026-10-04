#ifndef DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
#define DATA_FEED_L2_MESSAGE_FILE_WRITER_H_

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aliasing.h"
#include "l2_message.h"

namespace data_feed {

// Appends serialized L2Messages of a single pair to
// `[symbol]-[start unix time].bin` while recording, and renames it to
// `[symbol]-[start unix time]-[end unix time].bin` on Close() or destruction.
// The '/' of the pair is dropped from the file name, e.g. "BTCUSD".
class L2MessageFileWriter {
 public:
  explicit L2MessageFileWriter(const std::string& symbol);

  L2MessageFileWriter(const L2MessageFileWriter&) = delete;
  L2MessageFileWriter& operator=(const L2MessageFileWriter&) = delete;

  ~L2MessageFileWriter();

  // Flushed per message so nothing is lost when the process is killed.
  void Write(const L2Message& message);

  // Closes the file and gives it its final name. Idempotent.
  void Close();

 private:
  std::string file_stem_;
  std::filesystem::path path_;
  std::ofstream output_;
  std::vector<std::byte> buffer_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
