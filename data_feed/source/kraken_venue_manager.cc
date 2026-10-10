#include "kraken_venue_manager.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "constants.h"
#include "kraken_credentials.h"
#include "kraken_l2_message_parser.h"
#include "kraken_message_size_util.h"
#include "kraken_pair_precision.h"
#include "l2_message_file_writer.h"
#include "venues.h"
#include "websocket_data_feed.h"

namespace data_feed {

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

KrakenVenueManager::VenueManager()
    : credentials_{KrakenCredentials::FromEnvironment()},
      signer_{*credentials_},
      parser_{raw_frames_queue_, status_},
      feed_{WebsocketDataFeed::Endpoints{
                .host = std::string{kKrakenWsL2Host},
                .port = std::string{kKrakenHttpsPort},
                .target = std::string{kKrakenWsL2Target}},
            std::string{kRawLogStem}, raw_frames_queue_, status_} {}

KrakenVenueManager::~VenueManager() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

void KrakenVenueManager::Connect() {
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

void KrakenVenueManager::Disconnect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

void KrakenVenueManager::Subscribe(const std::string& symbol, u64 depth) {
  if (!std::ranges::contains(kKrakenL2Depths, depth)) {
    throw std::invalid_argument{
        std::format("Kraken has no L2 depth {} (see kKrakenL2Depths)", depth)};
  }
  // Fetched before locking, so the REST round trip blocks no one else.
  const PairPrecision precision =
      FetchKrakenPairPrecision(std::array{symbol}).at(symbol);

  const std::scoped_lock lock{life_cycle_mutex_};
  const auto existing = subscriptions_.find(symbol);
  if (existing != subscriptions_.end() && existing->second.depth == depth) {
    return;
  }
  // Kraken keeps one book per pair, so another depth is dropped first.
  UnsubscribeLocked(symbol);

  const auto subscription =
      subscriptions_
          .emplace(
              symbol,
              Subscription{.depth = depth,
                           .receiver = std::make_unique<L2MessageFileWriter>(
                               kVenueKraken, symbol, static_cast<u32>(depth),
                               precision)})
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

void KrakenVenueManager::Unsubscribe(std::string_view symbol) {
  const std::scoped_lock lock{life_cycle_mutex_};
  UnsubscribeLocked(symbol);
}

void KrakenVenueManager::DisconnectLocked() {
  // The feed goes first: it pushes into the frame queue until it stops, and
  // the parser drains that queue before it stops.
  feed_.Disconnect();
  parser_.Stop();
  // Only non-empty if the parser died mid-way; such frames are stale for the
  // next connection.
  raw_frames_queue_.reset();
  connected_ = false;
}

void KrakenVenueManager::UnsubscribeLocked(std::string_view symbol) {
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

}  // namespace data_feed
