#include "timing/aggregator.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include "core/aliasing.h"

namespace timing {

namespace {

// How long the writer sleeps after a pass that found nothing queued.
constexpr std::chrono::milliseconds kIdleSleep{1};

}  // namespace

Aggregator::ProducerHandle::~ProducerHandle() {
  producer->retired.store(true, std::memory_order_release);
}

void Aggregator::Start(const std::filesystem::path& path) {
  Stop();
  std::ofstream output{path, std::ios::trunc};
  if (!output) {
    throw std::runtime_error(
        std::format("Cannot open timing file {}", path.string()));
  }
  dropped_ = 0;
  writer_ = std::jthread{
      [output = std::move(output)](const std::stop_token& stop) mutable {
        Run(stop, std::move(output));
      }};
  running_ = true;
}

void Aggregator::Stop() {
  running_ = false;
  // Move-assigning requests the old thread's stop and joins it.
  writer_ = std::jthread{};
}

void Aggregator::Record(Interval interval, i64 nanos) {
  if (!running_.load(std::memory_order_relaxed)) {
    return;
  }
  if (!LocalProducer().queue.push(
          Sample{.interval = interval, .nanos = nanos})) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
  }
}

Aggregator::Producer& Aggregator::LocalProducer() {
  thread_local const ProducerHandle handle{[] {
    auto producer = std::make_shared<Producer>();
    const std::scoped_lock lock{producers_mutex_};
    producers_.push_back(producer);
    return producer;
  }()};
  return *handle.producer;
}

void Aggregator::Run(const std::stop_token& stop, std::ofstream output) {
  std::string buffer;
  // Read before the pass, so the pass after the stop request is a full one.
  for (bool stopping = false; !stopping;) {
    stopping = stop.stop_requested();
    DrainInto(buffer);
    if (buffer.empty()) {
      if (!stopping) {
        std::this_thread::sleep_for(kIdleSleep);
      }
      continue;
    }
    output << buffer;
    output.flush();
    buffer.clear();
  }
  output << std::format("# dropped {}\n", dropped_.load());
}

void Aggregator::DrainInto(std::string& buffer) {
  const std::scoped_lock lock{producers_mutex_};
  // The predicate drains each queue as it decides on it. Retirement is read
  // first: a producer retired by then has pushed its last sample, so the
  // drain that follows empties it for good.
  std::erase_if(producers_, [&buffer](const std::shared_ptr<Producer>& p) {
    const bool retired = p->retired.load(std::memory_order_acquire);
    p->queue.consume_all([&buffer](const Sample& sample) {
      std::format_to(std::back_inserter(buffer), "{} {}\n",
                     Name(sample.interval), sample.nanos);
    });
    return retired;
  });
}

}  // namespace timing
