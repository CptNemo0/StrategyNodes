#ifndef DATA_FEED_PIPELINE_STATUS_H_
#define DATA_FEED_PIPELINE_STATUS_H_

#include <atomic>

namespace data_feed {

// Shared by a venue's feed and parser threads. Each component stops its own
// thread; this only records that one of them died.
struct PipelineStatus {
  // Called by a thread that cannot continue.
  void Fail() { failed = true; }

  // Only call while neither thread is running.
  void Reset() { failed = false; }

  // Producers stop waiting for queue space once set, since a dead consumer
  // would never free it.
  std::atomic<bool> failed{false};
};

}  // namespace data_feed

#endif  // ! DATA_FEED_PIPELINE_STATUS_H_
