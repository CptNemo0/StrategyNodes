#ifndef DATA_FEED_SPSC_UTIL_H_
#define DATA_FEED_SPSC_UTIL_H_

#include <atomic>
#include <exception>
#include <print>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

namespace data_feed {

// Never drops: waits for the consumer to free a slot. Gives up only once
// `give_up` is set, so the caller decides what failure scope that is (the
// whole pipeline, or just this queue's consumer).
template <typename Queue, typename T>
void PushBlocking(Queue& queue, T&& value, const std::atomic<bool>& give_up) {
  while (!queue.push(std::forward<T>(value)) && !give_up) {
    std::this_thread::yield();
  }
}

// Consumes until stop is requested and the queue is empty, so nothing pushed
// before the stop is lost. On a fatal exception, calls `on_fail` and returns
// -- the caller decides what that failure means for the rest of the pipeline.
template <typename Queue, typename Consumer, typename OnFail>
void Drain(const std::stop_token& stop,
           std::string_view name,
           Queue& queue,
           const Consumer& consume,
           OnFail&& on_fail) {
  try {
    while (!stop.stop_requested() || queue.read_available() > 0) {
      if (!queue.consume_one(consume)) {
        std::this_thread::yield();
      }
    }
  } catch (const std::exception& e) {
    std::println("Fatal in {}: {}", name, e.what());
    on_fail();
  }
}

}  // namespace data_feed

#endif  // ! DATA_FEED_SPSC_UTIL_H_
