#ifndef DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
#define DATA_FEED_L2_MESSAGE_FILE_WRITER_H_

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"

namespace data_feed {

// Appends serialized L2Messages of a single pair to
// `[symbol]-[start unix time].bin` while recording, and renames it to
// `[symbol]-[start unix time]-[end unix time].bin` on Close() or destruction.
// The '/' of the pair is dropped from the file name, e.g. "BTCUSD".
//
// The file opens with an L2FileHeader carrying the depth and precision, which
// is the only record of what the integers in the messages mean.
class L2MessageFileWriter {
 public:
  L2MessageFileWriter(const std::string& symbol,
                      u32 depth,
                      const PairPrecision& precision);

  L2MessageFileWriter(const L2MessageFileWriter&) = delete;
  L2MessageFileWriter& operator=(const L2MessageFileWriter&) = delete;

  ~L2MessageFileWriter();

  // Flushed per message so nothing is lost when the process is killed.
  void Write(const L2Message& message);

  // Closes the file and gives it its final name. Idempotent.
  void Close();

 private:
  // Declared first: the file name and the header both derive from it, so they
  // agree on when the recording started.
  i64 start_time_ns_;
  std::string file_stem_;
  std::filesystem::path path_;
  std::ofstream output_;
  std::vector<std::byte> buffer_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_MESSAGE_FILE_WRITER_H_
