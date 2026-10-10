#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <print>
#include <thread>

#include "aliasing.h"
#include "kraken_venue_manager.h"

namespace {

// Global so the Ctrl+C handler can reach it. Each venue has its own
// PipelineStatus; this only tells main to tear the venues down.
std::atomic<bool> stop_requested{false};

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { stop_requested = true; });

  try {
    data_feed::KrakenVenueManager kraken;
    kraken.Connect();
    kraken.Subscribe(/*symbol=*/"BTC/USD", /*depth*/ 100);
    kraken.Subscribe(/*symbol=*/"ETH/USD", /*depth*/ 100);

    while (!stop_requested && !kraken.failed()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
