#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <memory>
#include <print>
#include <thread>
#include <utility>

#include "frontend/outbox.h"
#include "frontend/server.h"
#include "kraken/venue_manager.h"
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
    venue::Router::Managers managers;
    managers.emplace(venue::Id::kKraken,
                     std::make_unique<kraken::VenueManager>());
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
  } catch (const std::exception& e) {
    std::println(stderr, "Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
