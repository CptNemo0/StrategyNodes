#ifndef DATA_FEED_PIPELINE_STATUS_H_
#define DATA_FEED_PIPELINE_STATUS_H_

#include <atomic>

namespace data_feed {

// Flags shared by every thread of the feed -> parser -> writers pipeline.
struct PipelineStatus {
  // Called by a thread that cannot continue; stops the whole pipeline.
  void Fail() {
    failed = true;
    stop_requested = true;
  }

  // Set on Ctrl+C or a failure, so the feed loop exits.
  std::atomic<bool> stop_requested{false};
  // Set only on a failure. Producers then stop waiting for space, since a dead
  // consumer would never free it.
  std::atomic<bool> failed{false};
};

}  // namespace data_feed

#endif  // ! DATA_FEED_PIPELINE_STATUS_H_
