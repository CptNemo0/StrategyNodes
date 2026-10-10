#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <print>
#include <thread>

#include "core/aliasing.h"
#include "frontend/transmitter.h"
#include "kraken/venue_manager.h"
#include "l2/file_writer.h"

namespace {

// Global so the Ctrl+C handler can reach it. Each venue has its own
// PipelineStatus; this only tells main to tear the venues down.
std::atomic<bool> stop_requested{false};

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { stop_requested = true; });

  try {
    kraken::VenueManager kraken_manager;
    kraken_manager.Connect();
    kraken_manager.Subscribe<l2::FileWriter>(/*symbol=*/"BTC/USD",
                                             /*depth*/ 100);
    kraken_manager.Subscribe<frontend::Transmitter>(/*symbol=*/"ETH/USD",
                                                    /*depth*/ 100);

    while (!stop_requested && !kraken_manager.failed()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
