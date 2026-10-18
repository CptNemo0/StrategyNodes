#include "kraken/venue_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/debug_log.h"
#include "core/unix_time.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/l2_message_parser.h"
#include "kraken/message_size.h"
#include "kraken/pair_precision.h"
#include "kraken/tradable_pairs.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/manager.h"

namespace kraken {

namespace {

constexpr std::string_view kRawLogStem = "kraken_l2_messages";

// The least time between two resubscribes of one pair, so a pair whose
// checksum keeps failing does not flood the venue with requests.
constexpr std::chrono::seconds kMinResubscribeInterval{1};

// How long ListPairs() serves the pairs it fetched before fetching them
// again. The full list is a response of some 700 KB, and pairs come and go
// rarely.
constexpr std::chrono::minutes kPairsLifetime{5};

// The longest symbol Subscribe() accepts. Kraken's run to a dozen characters
// or so; anything far longer is not one.
constexpr std::size_t kMaxSymbolLength = 32;

// Whether `symbol` could name a Kraken pair, e.g. "BTC/USD" or "ETH2.S/ETH".
// A symbol goes into the AssetPairs query and the book requests as it is, so
// one from a client must not be able to add anything to either.
bool IsWellFormedSymbol(std::string_view symbol) {
  return !symbol.empty() && symbol.size() <= kMaxSymbolLength &&
         std::ranges::all_of(symbol, [](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                  c == '/' || c == '.' || c == '-' || c == '_';
         });
}

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

venue::Error MakeError(venue::Error::Code code, std::string detail) {
  return venue::Error{.code = code, .detail = std::move(detail)};
}

// The pair's precision, or why it could not be had.
std::expected<l2::PairPrecision, venue::Error> FetchPrecision(
    const std::string& symbol) {
  try {
    return FetchPairPrecision(std::array{symbol}).at(symbol);
  } catch (const UnknownPairError& e) {
    return std::unexpected{
        MakeError(venue::Error::Code::kUnknownSymbol, e.what())};
  } catch (const std::exception& e) {
    return std::unexpected{
        MakeError(venue::Error::Code::kVenueFailed, e.what())};
  }
}

}  // namespace

VenueManager::VenueManager()
    : credentials_{Credentials::FromEnvironment()},
      signer_{*credentials_},
      parser_{raw_frames_queue_, status_,
              [this](std::string_view symbol) { RequestResubscribe(symbol); }},
      feed_{net::WebsocketFeed::Endpoints{.host = std::string{kWsL2Host},
                                          .port = std::string{kHttpsPort},
                                          .target = std::string{kWsL2Target}},
            std::string{kRawLogStem}, raw_frames_queue_, status_},
      resubscriber_{
          [this](const std::stop_token& stop) { ResubscribeQueued(stop); }} {}

VenueManager::~VenueManager() {
  // First, so it does not resubscribe on a connection being torn down.
  resubscriber_.request_stop();
  resubscriber_.join();
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

  for (const Subscription& subscription : subscriptions_.values()) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kSubscribe,
                                subscription.book.symbol,
                                subscription.book.depth));
  }
  DEBUG_LOG("Kraken connected, resubscribing {} pairs", subscriptions_.size());
}

void VenueManager::Disconnect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

bool VenueManager::healthy() const {
  const std::scoped_lock lock{life_cycle_mutex_};
  return connected_ && !status_.failed;
}

std::expected<venue::SubscriptionId, venue::Error> VenueManager::Subscribe(
    venue::SubscriptionId id,
    l2::BookSpec book,
    venue::ReceiverFactoryCallback make_receiver) {
  if (!std::ranges::contains(kL2Depths, book.depth)) {
    return std::unexpected{
        MakeError(venue::Error::Code::kInvalidDepth,
                  std::format("Kraken has no L2 depth {}, only {}", book.depth,
                              kL2Depths))};
  }
  if (!IsWellFormedSymbol(book.symbol)) {
    return std::unexpected{MakeError(
        venue::Error::Code::kUnknownSymbol,
        std::format("A Kraken pair is up to {} letters, digits and '/', '.', "
                    "'-' or '_', e.g. BTC/USD",
                    kMaxSymbolLength))};
  }
  // Checked before the REST round trip so a doomed call fails fast. The round
  // trip itself runs unlocked, so it blocks no one else.
  {
    const std::scoped_lock lock{life_cycle_mutex_};
    if (const Taken taken = ResolveTakenLocked(id, book)) {
      return *taken;
    }
  }
  const std::expected<l2::PairPrecision, venue::Error> precision =
      FetchPrecision(book.symbol);
  if (!precision) {
    return std::unexpected{precision.error()};
  }

  const std::scoped_lock lock{life_cycle_mutex_};
  // Again: another call may have taken them during the round trip.
  if (const Taken taken = ResolveTakenLocked(id, book)) {
    return *taken;
  }
  const auto subscription =
      subscriptions_
          .emplace(id, Subscription{.book = std::move(book),
                                    .start_time_ns = core::UnixNanosNow(),
                                    .receiver = make_receiver(*precision)})
          .first;
  const l2::BookSpec& added = subscription->second.book;
  try {
    // The parser knows where the messages go before the venue sends any.
    parser_.Attach(added.symbol, *precision, added.depth,
                   *subscription->second.receiver);
  } catch (...) {
    subscriptions_.erase(subscription);
    throw;
  }
  if (connected_) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kSubscribe, added.symbol,
                                added.depth));
  }
  DEBUG_LOG("Subscription {}: {} {}@{}", id, venue::SinkName(venue::SinkOf(id)),
            added.symbol, added.depth);
  return id;
}

std::expected<void, venue::Error> VenueManager::Unsubscribe(
    venue::SubscriptionId id) {
  const std::scoped_lock lock{life_cycle_mutex_};
  const auto subscription = subscriptions_.find(id);
  if (subscription == subscriptions_.end()) {
    return std::unexpected{MakeError(venue::Error::Code::kUnknownId,
                                     std::format("No subscription {}", id))};
  }
  const l2::BookSpec& removed = subscription->second.book;
  // Frames already in flight are ignored from here on, so the receiver can go.
  parser_.Detach(removed.symbol);
  if (connected_) {
    feed_.Send(BuildBookRequest(SubscriptionMethod::kUnsubscribe,
                                removed.symbol, removed.depth));
  }
  DEBUG_LOG("Subscription {} ended: {}", id, removed.symbol);
  subscriptions_.erase(subscription);
  return {};
}

std::vector<venue::SubscriptionInfo> VenueManager::List() const {
  const std::scoped_lock lock{life_cycle_mutex_};
  return std::views::zip(subscriptions_.keys(), subscriptions_.values()) |
         std::views::transform([](const auto& entry) {
           const auto& [id, subscription] = entry;
           return venue::SubscriptionInfo{
               .id = id,
               .book = subscription.book,
               .start_time_ns = subscription.start_time_ns};
         }) |
         std::ranges::to<std::vector>();
}

std::expected<venue::PairList, venue::Error> VenueManager::ListPairs() {
  const std::scoped_lock lock{pairs_mutex_};
  const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
  if (pairs_.empty() || now - pairs_fetched_at_ >= kPairsLifetime) {
    try {
      pairs_ = FetchTradablePairs();
    } catch (const std::exception& e) {
      return std::unexpected{
          MakeError(venue::Error::Code::kVenueFailed, e.what())};
    }
    pairs_fetched_at_ = now;
    DEBUG_LOG("Fetched {} tradable Kraken pairs", pairs_.size());
  }
  return venue::PairList{.symbols = pairs_,
                         .depths = kL2Depths |
                                   std::views::transform([](u64 depth) {
                                     return static_cast<u32>(depth);
                                   }) |
                                   std::ranges::to<std::vector>()};
}

VenueManager::Taken VenueManager::ResolveTakenLocked(
    venue::SubscriptionId id,
    const l2::BookSpec& book) const {
  if (subscriptions_.contains(id)) {
    throw std::logic_error{
        std::format("Subscription id {} is already in use", id)};
  }
  const auto taken = FindSymbolLocked(book.symbol);
  if (taken == subscriptions_.end()) {
    return std::nullopt;
  }
  const u32 existing_depth = taken->second.book.depth;
  // Recording the same book twice would only duplicate the file.
  if (venue::SinkOf(id) == venue::Sink::kRecord &&
      venue::SinkOf(taken->first) == venue::Sink::kRecord &&
      existing_depth == book.depth) {
    return taken->first;
  }
  return std::unexpected{MakeError(
      venue::Error::Code::kSymbolInUse,
      std::format("{} is already subscribed to by subscription {} at depth {}",
                  book.symbol, taken->first, existing_depth))};
}

VenueManager::Subscriptions::const_iterator VenueManager::FindSymbolLocked(
    std::string_view symbol) const {
  return std::ranges::find(subscriptions_, symbol,
                           [](const auto& entry) -> std::string_view {
                             return entry.second.book.symbol;
                           });
}

void VenueManager::RequestResubscribe(std::string_view symbol) {
  {
    const std::scoped_lock lock{resubscribe_mutex_};
    if (std::ranges::contains(pending_resubscribes_, symbol)) {
      return;
    }
    pending_resubscribes_.emplace_back(symbol);
  }
  resubscribe_wake_.notify_one();
}

void VenueManager::ResubscribeQueued(const std::stop_token& stop) {
  // When each pair was last resubscribed.
  std::flat_map<std::string, std::chrono::steady_clock::time_point, std::less<>>
      last_resubscribed;
  std::unique_lock lock{resubscribe_mutex_};
  while (resubscribe_wake_.wait(
      lock, stop, [this] { return !pending_resubscribes_.empty(); })) {
    const std::string symbol = std::move(pending_resubscribes_.front());
    pending_resubscribes_.erase(pending_resubscribes_.begin());
    if (const auto last = last_resubscribed.find(symbol);
        last != last_resubscribed.end()) {
      // Waits out the interval, waking early only to stop.
      resubscribe_wake_.wait_until(lock, stop,
                                   last->second + kMinResubscribeInterval,
                                   [] { return false; });
      if (stop.stop_requested()) {
        return;
      }
    }
    // Not held while waiting on the life cycle lock, so the parser can
    // always queue another pair.
    lock.unlock();
    Resubscribe(symbol);
    last_resubscribed.insert_or_assign(symbol,
                                       std::chrono::steady_clock::now());
    lock.lock();
  }
}

void VenueManager::Resubscribe(std::string_view symbol) {
  const std::scoped_lock lock{life_cycle_mutex_};
  const auto subscription = FindSymbolLocked(symbol);
  if (!connected_ || subscription == subscriptions_.end()) {
    return;
  }
  DEBUG_LOG("Resubscribing {}", symbol);
  // Kraken handles the two in order, so the pair's next frame is the new
  // subscription's snapshot; the parser drops its updates until then.
  feed_.Send(BuildBookRequest(SubscriptionMethod::kUnsubscribe, symbol,
                              subscription->second.book.depth));
  feed_.Send(BuildBookRequest(SubscriptionMethod::kSubscribe, symbol,
                              subscription->second.book.depth));
}

void VenueManager::DisconnectLocked() {
  // The feed goes first: it pushes into the frame queue until it stops, and
  // the parser drains that queue before it stops.
  feed_.Disconnect();
  parser_.Stop();
  // Only non-empty if the parser died mid-way; such frames are stale for the
  // next connection.
  raw_frames_queue_.reset();
  // The parser has stopped, so no Receive() runs alongside. Only a connection
  // that was up ends: a failed attempt to connect carried no messages.
  if (connected_) {
    DEBUG_LOG("Kraken disconnected, {} subscriptions see a gap",
              subscriptions_.size());
    for (const Subscription& subscription : subscriptions_.values()) {
      subscription.receiver->OnGap(core::Gap::kDisconnected);
    }
  }
  connected_ = false;
}

}  // namespace kraken
