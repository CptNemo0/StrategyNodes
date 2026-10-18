#include "venue/l2_manager.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <flat_map>
#include <format>
#include <functional>
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
#include "core/receiver.h"
#include "core/unix_time.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/adapter.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace venue {

namespace {

// The least time between two resubscribes of one pair, so a pair whose
// checksum keeps failing does not flood the venue with requests.
constexpr std::chrono::seconds kMinResubscribeInterval{1};

// How long ListPairs() serves the pairs it fetched before fetching them
// again. Full lists run to hundreds of KB (Kraken's is some 700 KB), and
// pairs come and go rarely.
constexpr std::chrono::minutes kPairsLifetime{5};

}  // namespace

L2Manager::L2Manager(std::unique_ptr<Adapter> adapter)
    : adapter_{std::move(adapter)},
      parser_{raw_frames_queue_, status_,
              adapter_->book_checksum() == nullptr
                  ? std::nullopt
                  : std::optional{L2Parser::ChecksumCheck{
                        .checksum = adapter_->book_checksum(),
                        .on_mismatch =
                            [this](std::string_view symbol) {
                              RequestResubscribe(symbol);
                            }}}},
      feed_{adapter_->endpoints(),
            std::format("{}_l2_messages", adapter_->name()), raw_frames_queue_,
            status_},
      resubscriber_{
          [this](const std::stop_token& stop) { ResubscribeQueued(stop); }} {}

L2Manager::~L2Manager() {
  // First, so it does not resubscribe on a connection being torn down.
  resubscriber_.request_stop();
  resubscriber_.join();
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

void L2Manager::Connect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  if (connected_ && !status_.failed) {
    return;
  }
  DisconnectLocked();

  status_.Reset();
  parser_.Start(adapter_->MakeDecoder());
  try {
    feed_.Connect();
  } catch (...) {
    parser_.Stop();
    throw;
  }
  connected_ = true;

  for (std::string& request : adapter_->ConnectRequests()) {
    feed_.Send(std::move(request));
  }
  for (const Subscription& subscription : subscriptions_.values()) {
    SendLocked(Adapter::BookRequest::kSubscribe, subscription.book);
  }
  DEBUG_LOG("{} connected, resubscribing {} pairs", adapter_->name(),
            subscriptions_.size());
}

void L2Manager::Disconnect() {
  const std::scoped_lock lock{life_cycle_mutex_};
  DisconnectLocked();
}

bool L2Manager::healthy() const {
  const std::scoped_lock lock{life_cycle_mutex_};
  return connected_ && !status_.failed;
}

std::expected<SubscriptionId, Error> L2Manager::Subscribe(
    SubscriptionId id,
    l2::BookSpec book,
    ReceiverFactoryCallback make_receiver) {
  if (const std::expected<void, Error> checked = CheckBook(book); !checked) {
    return std::unexpected{checked.error()};
  }
  // Checked before the REST round trip so a doomed call fails fast. The round
  // trip itself runs unlocked, so it blocks no one else.
  {
    const std::scoped_lock lock{life_cycle_mutex_};
    if (const Taken taken = ResolveTakenLocked(id, book)) {
      return *taken;
    }
  }
  const std::expected<l2::PairPrecision, Error> precision =
      adapter_->FetchPrecision(book.symbol);
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
  SendLocked(Adapter::BookRequest::kSubscribe, added);
  DEBUG_LOG("Subscription {}: {} {} {}@{}", id, SinkName(SinkOf(id)),
            adapter_->name(), added.symbol, added.depth);
  return id;
}

std::expected<void, Error> L2Manager::Unsubscribe(SubscriptionId id) {
  const std::scoped_lock lock{life_cycle_mutex_};
  const auto subscription = subscriptions_.find(id);
  if (subscription == subscriptions_.end()) {
    return Failure(Error::Code::kUnknownId,
                   std::format("No subscription {}", id));
  }
  const l2::BookSpec& removed = subscription->second.book;
  // Frames already in flight are ignored from here on, so the receiver can go.
  parser_.Detach(removed.symbol);
  SendLocked(Adapter::BookRequest::kUnsubscribe, removed);
  DEBUG_LOG("Subscription {} ended: {}", id, removed.symbol);
  subscriptions_.erase(subscription);
  return {};
}

std::vector<SubscriptionInfo> L2Manager::List() const {
  const std::scoped_lock lock{life_cycle_mutex_};
  return std::views::zip(subscriptions_.keys(), subscriptions_.values()) |
         std::views::transform([](const auto& entry) {
           const auto& [id, subscription] = entry;
           return SubscriptionInfo{.id = id,
                                   .book = subscription.book,
                                   .start_time_ns = subscription.start_time_ns};
         }) |
         std::ranges::to<std::vector>();
}

std::expected<PairList, Error> L2Manager::ListPairs() {
  const std::scoped_lock lock{pairs_mutex_};
  const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
  if (pairs_.empty() || now - pairs_fetched_at_ >= kPairsLifetime) {
    std::expected<std::vector<std::string>, Error> fetched =
        adapter_->FetchPairs();
    if (!fetched) {
      return std::unexpected{std::move(fetched.error())};
    }
    pairs_ = std::move(*fetched);
    pairs_fetched_at_ = now;
    DEBUG_LOG("Fetched {} tradable {} pairs", pairs_.size(), adapter_->name());
  }
  return PairList{
      .symbols = pairs_,
      .depths = adapter_->depths() | std::ranges::to<std::vector>()};
}

std::expected<void, Error> L2Manager::CheckBook(
    const l2::BookSpec& book) const {
  if (!std::ranges::contains(adapter_->depths(), book.depth)) {
    return Failure(
        Error::Code::kInvalidDepth,
        std::format("{} has no L2 depth {}, only {}", adapter_->name(),
                    book.depth, adapter_->depths()));
  }
  return adapter_->CheckSymbol(book.symbol);
}

L2Manager::Taken L2Manager::ResolveTakenLocked(SubscriptionId id,
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
  if (SinkOf(id) == Sink::kRecord && SinkOf(taken->first) == Sink::kRecord &&
      existing_depth == book.depth) {
    return taken->first;
  }
  return Failure(
      Error::Code::kSymbolInUse,
      std::format("{} is already subscribed to by subscription {} at depth {}",
                  book.symbol, taken->first, existing_depth));
}

L2Manager::Subscriptions::const_iterator L2Manager::FindSymbolLocked(
    std::string_view symbol) const {
  return std::ranges::find(subscriptions_, symbol,
                           [](const auto& entry) -> std::string_view {
                             return entry.second.book.symbol;
                           });
}

void L2Manager::SendLocked(Adapter::BookRequest request,
                           const l2::BookSpec& book) {
  if (connected_) {
    feed_.Send(adapter_->MakeBookRequest(request, book));
  }
}

void L2Manager::RequestResubscribe(std::string_view symbol) {
  {
    const std::scoped_lock lock{resubscribe_mutex_};
    if (std::ranges::contains(pending_resubscribes_, symbol)) {
      return;
    }
    pending_resubscribes_.emplace_back(symbol);
  }
  resubscribe_wake_.notify_one();
}

void L2Manager::ResubscribeQueued(const std::stop_token& stop) {
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

void L2Manager::Resubscribe(std::string_view symbol) {
  const std::scoped_lock lock{life_cycle_mutex_};
  const auto subscription = FindSymbolLocked(symbol);
  if (!connected_ || subscription == subscriptions_.end()) {
    return;
  }
  DEBUG_LOG("Resubscribing {} {}", adapter_->name(), symbol);
  // The venue handles the two in order, so the pair's next frame is the new
  // subscription's snapshot; the parser drops its updates until then.
  SendLocked(Adapter::BookRequest::kUnsubscribe, subscription->second.book);
  SendLocked(Adapter::BookRequest::kSubscribe, subscription->second.book);
}

void L2Manager::DisconnectLocked() {
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
    DEBUG_LOG("{} disconnected, {} subscriptions see a gap", adapter_->name(),
              subscriptions_.size());
    for (const Subscription& subscription : subscriptions_.values()) {
      subscription.receiver->OnGap(core::Gap::kDisconnected);
    }
  }
  connected_ = false;
}

}  // namespace venue
