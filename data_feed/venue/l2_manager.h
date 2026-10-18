#ifndef DATA_FEED_VENUE_L2_MANAGER_H_
#define DATA_FEED_VENUE_L2_MANAGER_H_

#include <chrono>
#include <condition_variable>
#include <expected>
#include <flat_map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/aliasing.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "l2/book_spec.h"
#include "l2/message.h"
#include "net/raw_frame.h"
#include "net/websocket_feed.h"
#include "venue/adapter.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace venue {

// One venue's L2 books, whichever venue its Adapter describes. Owns everything
// needed to stream them: a generic websocket feed and an L2Parser decoding
// with the adapter's decoder, plus the PipelineStatus their threads share.
// Wires a receiver between them for each subscription.
//
// A pair has at most one subscription: a venue may stream a pair at one depth
// per connection, which sharing would force on every receiver, and a receiver
// joining mid-stream would start without a snapshot.
//
// Construction does not connect; Connect() does, and the Router calls it
// again whenever the manager is not healthy().
//
// If the venue sends checksums, the parser checks every pair's book against
// them. A pair that fails is resubscribed on the live connection,
// unsubscribing and then subscribing again for a fresh snapshot, by a thread
// of the manager's own, so the parser never waits on the manager's lock. A
// pair that keeps failing is resubscribed at most once per second.
//
// Every public method may be called any number of times, in any order and from
// any thread; each leaves the manager either fully connected or fully
// disconnected, and each subscription either fully set up or fully torn down,
// even when it throws. Subscriptions outlive connections: they are sent again
// on every Connect(). Destruction disconnects and drops every subscription.
class L2Manager final : public Manager {
 public:
  explicit L2Manager(std::unique_ptr<Adapter> adapter);

  L2Manager(const L2Manager&) = delete;
  L2Manager& operator=(const L2Manager&) = delete;

  ~L2Manager() override;

  std::string_view name() const override { return adapter_->name(); }

  // Starts the parser, connects the feed and subscribes to every pair. Does
  // nothing while healthily connected; after a failure, tears the broken
  // connection down first. If it throws, the manager is left disconnected.
  void Connect() override;

  // Stops the feed, then the parser once it has parsed every frame read, then
  // tells every receiver the connection ended. Subscriptions are kept. Does
  // nothing while disconnected.
  void Disconnect();

  // Connected, and neither the feed nor the parser died since.
  bool healthy() const override;

  // Starts with a snapshot. The depth must be one of the adapter's. Fetches
  // the pair's precision from the venue, so it blocks for a REST round trip.
  // Throws std::logic_error if `id` is in use.
  std::expected<SubscriptionId, Error> Subscribe(
      SubscriptionId id,
      l2::BookSpec book,
      ReceiverFactoryCallback make_receiver) override;

  // Also unsubscribes from the pair.
  std::expected<void, Error> Unsubscribe(SubscriptionId id) override;

  std::vector<SubscriptionInfo> List() const override;

  // Fetches the pairs from the venue, so it blocks for a REST round trip,
  // unless they were fetched recently.
  std::expected<PairList, Error> ListPairs() override;

 private:
  // The one subscriber to a pair. Its sink is SinkOf() of its id.
  struct Subscription {
    l2::BookSpec book;
    i64 start_time_ns;
    std::unique_ptr<core::Receiver<l2::Message>> receiver;
  };

  using Subscriptions = std::flat_map<SubscriptionId, Subscription>;

  // What Subscribe() returns early when the symbol is taken, or nothing.
  using Taken = std::optional<std::expected<SubscriptionId, Error>>;

  // Fails if Subscribe() must refuse `book` without asking the venue.
  std::expected<void, Error> CheckBook(const l2::BookSpec& book) const;

  // What Subscribe() returns when `id` or the book's symbol are already
  // taken: the existing recording's id if `id` is a recording's too and the
  // pair is recorded at the book's depth, or else kSymbolInUse. Nothing if
  // the subscription may go ahead. Throws std::logic_error if `id` is in use,
  // which only a caller bug does.
  Taken ResolveTakenLocked(SubscriptionId id, const l2::BookSpec& book) const;

  // The subscription to `symbol`, or end().
  Subscriptions::const_iterator FindSymbolLocked(std::string_view symbol) const;

  // Sends the request for `book` to the venue.
  void SendLocked(Adapter::BookRequest request, const l2::BookSpec& book);

  // Disconnect() without the lock.
  void DisconnectLocked();

  // Queues `symbol` for the resubscriber, once. Called by the parser, on its
  // thread, when the pair's book fails its checksum.
  void RequestResubscribe(std::string_view symbol);

  // Resubscriber thread body: resubscribes each queued pair until `stop`.
  void ResubscribeQueued(const std::stop_token& stop);

  // Unsubscribes from `symbol` and subscribes to it again, if it is still
  // subscribed and connected; a new connection subscribes afresh anyway.
  void Resubscribe(std::string_view symbol);

  const std::unique_ptr<const Adapter> adapter_;
  // Serializes every public method but name().
  mutable std::mutex life_cycle_mutex_;
  core::PipelineStatus status_;
  net::RawFrameQueue raw_frames_queue_;
  // Declared before the parser, which points into it, so it outlives it.
  Subscriptions subscriptions_;
  L2Parser parser_;
  // Declared after the parser so it is destroyed first: it feeds the parser.
  net::WebsocketFeed feed_;
  bool connected_{false};
  // Pairs waiting to be resubscribed, in the order their checksums failed.
  std::mutex resubscribe_mutex_;
  std::condition_variable_any resubscribe_wake_;
  std::vector<std::string> pending_resubscribes_;
  // The tradable pairs as last fetched, and when. Under pairs_mutex_, which is
  // held across a fetch, so callers arriving meanwhile wait for its result
  // rather than each fetching the list again. Independent of
  // life_cycle_mutex_, so a fetch blocks no subscription.
  std::mutex pairs_mutex_;
  std::vector<std::string> pairs_;
  std::chrono::steady_clock::time_point pairs_fetched_at_;
  // Stopped first thing in the destructor.
  std::jthread resubscriber_;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_L2_MANAGER_H_
