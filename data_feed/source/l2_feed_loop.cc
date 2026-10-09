#include "l2_feed_loop.h"

#include <flat_set>
#include <print>
#include <string>
#include <utility>

#include "spsc_util.h"

namespace data_feed {

void RunL2FeedLoop(Level2KrakenDataFeed& feed,
                    FrameQueue& frames,
                    const WriterLaneMap& lanes,
                    std::ofstream& output,
                    PipelineStatus& status) {
  std::flat_set<std::string> unsubscribed;

  // Heartbeats arrive every second, so the flag is checked regularly.
  while (!status.stop_requested) {
    // A lane failure only kills that pair's lane (see L2MessageWriterLane);
    // unsubscribing here is what actually stops the feed sending it data.
    for (const auto& [symbol, lane] : lanes) {
      if (lane->failed() && !unsubscribed.contains(symbol)) {
        std::println("Lane for {} failed; unsubscribing", symbol);
        feed.Unsubscribe(symbol);
        unsubscribed.insert(symbol);
      }
    }

    std::string frame = feed.Next();
    std::println(output, "{}", frame);
    output.flush();
    PushBlocking(frames, std::move(frame), status.failed);
  }

  feed.Close();
}

}  // namespace data_feed
