#include "venue/router.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <expected>
#include <flat_map>
#include <format>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/outbox.h"
#include "frontend/transmitter.h"
#include "l2/file_writer.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace venue {

namespace {

// How often the supervisor checks whether a venue needs reconnecting.
constexpr std::chrono::milliseconds kSupervisePeriod{100};

// Builds the receiver of subscription `id`: a frontend::Transmitter bound to
// `outbox` for kLive, an l2::FileWriter for kRecord.
template <Sink sink>
ReceiverFactory MakeReceiverFactory(frontend::Outbox& outbox,
                                    SubscriptionId id,
                                    std::string_view venue,
                                    const std::string& symbol,
                                    u32 depth) {
  return [&outbox, id, venue, symbol, depth](const l2::PairPrecision& precision)
             -> std::unique_ptr<core::Receiver<l2::Message>> {
    if constexpr (sink == Sink::kRecord) {
      return std::make_unique<l2::FileWriter>(venue, symbol, depth, precision);
    } else {
      return std::make_unique<frontend::Transmitter>(outbox, id, venue, symbol,
                                                     depth, precision);
    }
  };
}

}  // namespace

Router::Router(Managers managers, frontend::Outbox& outbox, Backoff backoff)
    : managers_{std::move(managers)},
      outbox_{outbox},
      backoff_{backoff},
      supervisor_{[this](const std::stop_token& stop) { Supervise(stop); }} {}

Router::~Router() = default;

template <Sink sink>
std::expected<SubscriptionId, Error>
Router::Subscribe(Id venue, const std::string& symbol, u32 depth) {
  const auto manager = managers_.find(venue);
  if (manager == managers_.end()) {
    return std::unexpected{
        Error{.code = Error::Code::kUnknownVenue,
              .detail = std::format("No venue {}", std::to_underlying(venue))}};
  }
  const SubscriptionId id = next_id_++;
  return manager->second->Subscribe(
      id, symbol, depth, sink,
      MakeReceiverFactory<sink>(outbox_, id, manager->second->name(), symbol,
                                depth));
}

template std::expected<SubscriptionId, Error>
Router::Subscribe<Sink::kLive>(Id venue, const std::string& symbol, u32 depth);
template std::expected<SubscriptionId, Error> Router::Subscribe<Sink::kRecord>(
    Id venue,
    const std::string& symbol,
    u32 depth);

std::expected<void, Error> Router::Unsubscribe(SubscriptionId id, Sink sink) {
  for (const std::unique_ptr<Manager>& manager : managers_.values()) {
    std::expected<void, Error> unsubscribed = manager->Unsubscribe(id, sink);
    // Any other answer comes from the venue that has the id.
    if (unsubscribed || unsubscribed.error().code != Error::Code::kUnknownId) {
      return unsubscribed;
    }
  }
  return std::unexpected{
      Error{.code = Error::Code::kUnknownId,
            .detail = std::format("No subscription {}", id)}};
}

std::vector<Router::Listing> Router::List() const {
  std::vector<Listing> listings;
  for (const auto& [venue, manager] : managers_) {
    listings.append_range(
        manager->List() |
        std::views::transform([venue](SubscriptionInfo subscription) {
          return Listing{.venue = venue,
                         .subscription = std::move(subscription)};
        }));
  }
  return listings;
}

void Router::Supervise(const std::stop_token& stop) {
  // When a venue may next be tried, and how long to wait if that try fails.
  struct Retry {
    std::chrono::steady_clock::time_point next_attempt;
    std::chrono::milliseconds delay;
  };

  std::flat_map<Id, Retry> retries =
      managers_ | std::views::keys | std::views::transform([this](Id venue) {
        return std::pair{venue,
                         Retry{.next_attempt = {}, .delay = backoff_.initial}};
      }) |
      std::ranges::to<std::flat_map<Id, Retry>>();
  std::mutex wake_mutex;
  std::condition_variable_any wake;

  while (!stop.stop_requested()) {
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    for (const auto& [venue, manager] : managers_) {
      Retry& retry = retries.at(venue);
      if (manager->healthy() || now < retry.next_attempt) {
        continue;
      }
      try {
        manager->Connect();
        retry.delay = backoff_.initial;
      } catch (const std::exception& e) {
        std::println(stderr, "Connecting to {} failed, retrying in {}: {}",
                     manager->name(), retry.delay, e.what());
        retry.next_attempt = now + retry.delay;
        retry.delay = std::min(retry.delay * 2, backoff_.max);
      }
    }
    // Sleeps, but wakes at once when stopped.
    std::unique_lock lock{wake_mutex};
    wake.wait_for(lock, stop, kSupervisePeriod, [] { return false; });
  }
}

}  // namespace venue
