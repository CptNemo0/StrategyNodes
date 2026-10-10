#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <expected>
#include <memory>
#include <print>
#include <string>
#include <thread>
#include <utility>

#include "core/aliasing.h"
#include "kraken/venue_manager.h"
#include "venue/manager.h"
#include "venue/router.h"

namespace {

// Global so the Ctrl+C handler can reach it. The Router keeps every venue
// connected; this only tells main to tear them down.
std::atomic<bool> stop_requested{false};

// Stands in for the frontend's commands until the server exists.
template <venue::Sink sink>
void SubscribeOrReport(venue::Router& router,
                       const std::string& symbol,
                       u32 depth) {
  const std::expected<venue::SubscriptionId, venue::Error> subscribed =
      router.Subscribe<sink>(venue::Id::kKraken, symbol, depth);
  if (subscribed) {
    std::println("Subscription {}: {}@{}", *subscribed, symbol, depth);
  } else {
    std::println(stderr, "Cannot subscribe to {}@{}: {}", symbol, depth,
                 subscribed.error().detail);
  }
}

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { stop_requested = true; });

  try {
    venue::Router::Managers managers;
    managers.emplace(venue::Id::kKraken,
                     std::make_unique<kraken::VenueManager>());
    venue::Router router{std::move(managers)};

    SubscribeOrReport<venue::Sink::kRecord>(router, "BTC/USD", /*depth=*/100);
    SubscribeOrReport<venue::Sink::kLive>(router, "ETH/USD", /*depth=*/100);

    while (!stop_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    std::println(stderr, "Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
