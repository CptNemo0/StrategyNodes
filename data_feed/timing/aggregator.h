#ifndef DATA_FEED_TIMING_AGGREGATOR_H_
#define DATA_FEED_TIMING_AGGREGATOR_H_

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/aliasing.h"

namespace timing {

// One measured stretch of the pipeline, named `k[From]To[To]`.
enum class Interval : u8 {
  // From the venue's own timestamp of an update to the parser thread taking
  // up its frame: the network, the feed thread and the frame queue. Across
  // two clocks, so it also holds their skew.
  kExchangeToParseStart,
  // One frame through its venue's L2Decoder, from the parser thread taking
  // it up to Decode() returning, every l2::Message delivered.
  kReceiveToParseComplete,
};

// How an Interval is spelled in the stats file.
constexpr std::string_view Name(Interval interval) {
  switch (interval) {
    case Interval::kExchangeToParseStart:
      return "exchange_to_parse_start";
    case Interval::kReceiveToParseComplete:
      return "receive_to_parse_complete";
  }
  std::unreachable();
}

// Collects timings from any thread and writes them to one stats file, a
// `[name] [nanoseconds]` line per sample, which timing_report.py summarizes.
//
// Record() never blocks or allocates once a thread is registered: each
// recording thread gets an SPSC queue of its own on its first Record(), and
// one writer thread drains all of them. A sample that finds its queue full is
// dropped and counted; the count closes the file as `# dropped [count]`.
//
// Everything is static, as there is one stats file per process.
class Aggregator {
 public:
  // Samples one thread can have queued before Record() drops.
  static constexpr std::size_t kQueueCapacity = 1 << 16;

  Aggregator() = delete;

  // Opens `path` (truncating it) and starts the writer thread. Throws if the
  // file cannot be opened. Record() is a no-op until then.
  static void Start(const std::filesystem::path& path);

  // Stops recording, writes out what is queued and closes the file.
  // Idempotent.
  static void Stop();

  // Queues one sample. Callable from any thread.
  static void Record(Interval interval, i64 nanos);

 private:
  struct Sample {
    Interval interval;
    i64 nanos;
  };

  using SampleQueue =
      boost::lockfree::spsc_queue<Sample,
                                  boost::lockfree::capacity<kQueueCapacity>>;

  // One recording thread's queue. Shared between the registry and the
  // thread, so whichever lets go last frees it.
  struct Producer {
    SampleQueue queue;
    // Set when the thread exits: once drained, the writer drops it.
    std::atomic<bool> retired{false};
  };

  // The thread_local owner of a thread's Producer, retiring it on thread
  // exit.
  struct ProducerHandle {
    explicit ProducerHandle(std::shared_ptr<Producer> producer)
        : producer(std::move(producer)) {}
    ProducerHandle(const ProducerHandle&) = delete;
    ProducerHandle& operator=(const ProducerHandle&) = delete;
    ~ProducerHandle();

    const std::shared_ptr<Producer> producer;
  };

  // The calling thread's Producer, registered on first use.
  static Producer& LocalProducer();

  // Writer thread body: drains every queue until stopped, then once more.
  static void Run(const std::stop_token& stop, std::ofstream output);

  // Formats every queued sample into `buffer`, dropping retired producers
  // once they are empty.
  static void DrainInto(std::string& buffer);

  static inline std::atomic<bool> running_{false};
  static inline std::atomic<u64> dropped_{0};
  // Guards producers_; taken only by a thread's first Record() and by the
  // writer.
  static inline std::mutex producers_mutex_;
  static inline std::vector<std::shared_ptr<Producer>> producers_;
  // Declared last so it is destroyed, and joined, first.
  static inline std::jthread writer_;
};

}  // namespace timing

#endif  // ! DATA_FEED_TIMING_AGGREGATOR_H_
