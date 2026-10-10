#include "kraken/venue_manager.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/l2_message_parser.h"
#include "kraken/message_size.h"
#include "kraken/pair_precision.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"

namespace kraken {

namespace {

constexpr std::string_view kRawLogStem = "kraken_l2_messages";

enum class SubscriptionMethod { kSubscribe, kUnsubscribe };

// A request on Kraken's "book" channel for one pair.
std::string BuildBookRequest(SubscriptionMethod method,
                             std::string_view symbol,
                             u64 depth) {
  return std::format(
      R"({{"method":"{}","params":{{"channel":"book","depth":{},"symbol":["{}"]}}}})",
      method == SubscriptionMethod::kSubscribe ? "subscribe" : "unsubscribe",
      depth, symbol);
}

}  // namespace

VenueManager::VenueManager()
    : credentials_{Credentials::FromEnvironment()},
      signer_{*credentials_},
      parser_{raw_frames_queue_, status_},
      feed_{net::WebsocketFeed::Endpoints{.host = std::string{kWsL2Host},
                                          .port = std::string{kHttpsPort},
                                          .target = std::string{kWsL2Target}},
            std::string{kRawLogStem}, raw_frames_queue_, status_} {}

VenueManager::~VenueManager() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

void VenueManager::Connect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  if (connected_ && !status_.failed) {
    return;
  }
  DisconnectLocked();

  status_.Reset();
  parser_.Start();
  try {
    feed_.Connect();
  } catch (...) {
    parser_.Stop();
    throw;
  }
  connected_ = true;

  for (const auto& [symbol, subscription] : subscriptions_) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kSubscribe, symbol,
                                subscription.depth));
  }
}

void VenueManager::Disconnect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

void VenueManager::SubscribeImpl(const std::string& symbol,
                                 u64 depth,
                                 ReceiverFactory make_receiver) {
  if (!std::ranges::contains(kL2Depths, depth)) {
    throw std::invalid_argument{
        std::format("Kraken has no L2 depth {} (see kL2Depths)", depth)};
  }
  // Fetched before locking, so the REST round trip blocks no one else.
  const l2::PairPrecision precision =
      FetchPairPrecision(std::array{symbol}).at(symbol);

  const std::scoped_lock lock{life_cycle_mutex_};
  const auto existing = subscriptions_.find(symbol);
  if (existing != subscriptions_.end() && existing->second.depth == depth) {
    return;
  }
  // Kraken keeps one book per pair, so another depth is dropped first.
  UnsubscribeLocked(symbol);

  const auto subscription =
      subscriptions_
          .emplace(symbol, Subscription{.depth = depth,
                                        .receiver = make_receiver(precision)})
          .first;
  try {
    // The parser knows where the messages go before the venue sends any.
    parser_.Attach(symbol, precision, *subscription->second.receiver);
  } catch (...) {
    subscriptions_.erase(subscription);
    throw;
  }
  if (connected_) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kSubscribe, symbol, depth));
  }
}

void VenueManager::Unsubscribe(std::string_view symbol) {
  const std::scoped_lock lock{life_cycle_mutex_};
  UnsubscribeLocked(symbol);
}

void VenueManager::DisconnectLocked() {
  // The feed goes first: it pushes into the frame queue until it stops, and
  // the parser drains that queue before it stops.
  feed_.Disconnect();
  parser_.Stop();
  // Only non-empty if the parser died mid-way; such frames are stale for the
  // next connection.
  raw_frames_queue_.reset();
  connected_ = false;
}

void VenueManager::UnsubscribeLocked(std::string_view symbol) {
  const auto subscription = subscriptions_.find(symbol);
  if (subscription == subscriptions_.end()) {
    return;
  }
  if (connected_) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kUnsubscribe, symbol,
                                subscription->second.depth));
  }
  // Frames already in flight are ignored from here on, so the receiver can go.
  parser_.Detach(symbol);
  subscriptions_.erase(subscription);
}

}  // namespace kraken
