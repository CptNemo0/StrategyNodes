#include "l2_feed_loop.h"

#include <flat_set>
#include <fstream>
#include <print>
#include <string>
#include <utility>

#include "aliasing.h"
#include "l2_kraken_data_feed.h"
#include "l2_message_dispatcher.h"
#include "pipeline_status.h"
#include "spsc_util.h"
#include "utility.h"

namespace data_feed {

void RunL2FeedLoop(Level2KrakenDataFeed& feed,
                   FrameQueue& frames,
                   const FileWriterMap& writers,
                   std::ofstream& output,
                   PipelineStatus& status) {
  std::flat_set<std::string> unsubscribed;

  // Heartbeats arrive every second, so the flag is checked regularly.
  while (!status.stop_requested) {
    // A writer failure only kills that pair's writer (see
    // L2MessageFileWriter); unsubscribing here is what actually stops the feed
    // sending it data.
    for (const auto& [symbol, writer] : writers) {
      if (writer->failed() && !unsubscribed.contains(symbol)) {
        std::println("Writer for {} failed; unsubscribing", symbol);
        feed.Unsubscribe(symbol);
        unsubscribed.insert(symbol);
      }
    }

    std::string frame = feed.Next();
    // Stamped before the log write below, so the capture time measures when
    // the frame arrived rather than when we finished writing it out.
    const i64 capture_time_ns = UnixNanosNow();

    std::println(output, "{}", frame);
    output.flush();
    PushBlocking(frames, RawFrame{std::move(frame), capture_time_ns},
                 status.failed);
  }

  feed.Close();
}

}  // namespace data_feed
