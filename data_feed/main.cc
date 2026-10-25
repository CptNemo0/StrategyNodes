#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <format>
#include <memory>
#include <print>
#include <thread>
#include <utility>

#include "coinbase/adapter.h"
#include "core/debug_log.h"
#include "core/unix_time.h"
#include "frontend/outbox.h"
#include "frontend/server.h"
#include "kraken/adapter.h"
#include "timing/aggregator.h"
#include "venue/l2_manager.h"
#include "venue/manager.h"
#include "venue/router.h"

namespace {

// Global so the Ctrl+C handler can reach it. The Router keeps every venue
// connected; this only tells main to tear them down.
std::atomic<bool> stop_requested{false};

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { stop_requested = true; });

  try {
    timing::Aggregator::Start(
        std::format("timings-{}.txt", core::UnixNanosNow()));
    venue::Router::Managers managers;
    managers.emplace(venue::Id::kKraken,
                     std::make_unique<venue::L2Manager>(
                         std::make_unique<kraken::Adapter>()));
    managers.emplace(venue::Id::kCoinbase,
                     std::make_unique<venue::L2Manager>(
                         std::make_unique<coinbase::Adapter>()));
    // Declared in this order, so the server, which calls the router, goes
    // first, and the outbox, which every live subscription registers with,
    // goes last.
    frontend::Outbox outbox;
    venue::Router router{std::move(managers), outbox};
    const frontend::Server server{router, outbox, frontend::ServerAddress{}};
    std::println("Frontend server listening on port {}", server.port());

    while (!stop_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    DEBUG_LOG("Stop requested, shutting down");
  } catch (const std::exception& e) {
    std::println(stderr, "Fatal: {}", e.what());
    return 1;
  }

  // After every venue is torn down, so the file holds their last samples.
  timing::Aggregator::Stop();
  return 0;
}
