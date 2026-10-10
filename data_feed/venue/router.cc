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
#include <type_traits>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/transmitter.h"
#include "l2/file_writer.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace venue {

namespace {

// How often the supervisor checks whether a venue needs reconnecting.
constexpr std::chrono::milliseconds kSupervisePeriod{100};

std::string_view SinkName(Sink sink) {
  return sink == Sink::kRecord ? "a recording" : "a live feed";
}

// Builds a `ReceiverT` for one subscription. FileWriter and Transmitter take
// the same arguments, so either can back a subscription.
template <typename ReceiverT>
ReceiverFactory MakeReceiverFactory(std::string_view venue,
                                    const std::string& symbol,
                                    u32 depth) {
  return [venue, symbol, depth](const l2::PairPrecision& precision)
             -> std::unique_ptr<core::Receiver<l2::Message>> {
    return std::make_unique<ReceiverT>(venue, symbol, depth, precision);
  };
}

}  // namespace

Router::Router(Managers managers, Backoff backoff)
    : managers_{std::move(managers)},
      backoff_{backoff},
      supervisor_{[this](const std::stop_token& stop) { Supervise(stop); }} {}

Router::~Router() = default;

template <Sink sink>
std::expected<SubscriptionId, Error>
Router::Subscribe(Id venue, const std::string& symbol, u32 depth) {
  using ReceiverT = std::conditional_t<sink == Sink::kRecord, l2::FileWriter,
                                       frontend::Transmitter>;
  const auto manager = managers_.find(venue);
  if (manager == managers_.end()) {
    return std::unexpected{
        Error{.code = Error::Code::kUnknownVenue,
              .detail = std::format("No venue {}", std::to_underlying(venue))}};
  }
  const std::string_view venue_name = manager->second->name();
  return manager->second
      ->Subscribe(next_id_++, symbol, depth, sink,
                  MakeReceiverFactory<ReceiverT>(venue_name, symbol, depth))
      .transform([&](SubscriptionId id) {
        const std::scoped_lock lock{owners_mutex_};
        // Already there if `id` is an existing recording's.
        owners_.try_emplace(id, Owner{.venue = venue, .sink = sink});
        return id;
      });
}

template std::expected<SubscriptionId, Error>
Router::Subscribe<Sink::kLive>(Id venue, const std::string& symbol, u32 depth);
template std::expected<SubscriptionId, Error> Router::Subscribe<Sink::kRecord>(
    Id venue,
    const std::string& symbol,
    u32 depth);

std::expected<void, Error> Router::Unsubscribe(SubscriptionId id, Sink sink) {
  const std::scoped_lock lock{owners_mutex_};
  const auto owner = owners_.find(id);
  if (owner == owners_.end()) {
    return std::unexpected{
        Error{.code = Error::Code::kUnknownId,
              .detail = std::format("No subscription {}", id)}};
  }
  if (owner->second.sink != sink) {
    return std::unexpected{Error{
        .code = Error::Code::kSinkMismatch,
        .detail = std::format("Subscription {} is {}, not {}", id,
                              SinkName(owner->second.sink), SinkName(sink))}};
  }
  std::expected<void, Error> unsubscribed =
      managers_.at(owner->second.venue)->Unsubscribe(id);
  // Gone either way: a manager that does not know the id streams nothing
  // under it.
  owners_.erase(owner);
  return unsubscribed;
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
