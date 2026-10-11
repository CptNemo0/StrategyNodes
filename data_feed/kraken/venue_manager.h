#ifndef DATA_FEED_KRAKEN_VENUE_MANAGER_H_
#define DATA_FEED_KRAKEN_VENUE_MANAGER_H_

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
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/l2_message_parser.h"
#include "kraken/websocket_token_generator.h"
#include "l2/message.h"
#include "net/raw_frame.h"
#include "net/websocket_feed.h"
#include "venue/manager.h"

namespace kraken {

// Kraken's L2 books. Owns everything needed to stream them: the credentials,
// a generic websocket feed and Kraken's L2 parser, plus the PipelineStatus
// their threads share. Wires a receiver between them for each subscription.
// Construction loads the credentials from the environment.
//
// Kraken streams a pair at one depth per connection, so a pair has at most one
// subscription: sharing it would force every receiver onto one depth.
//
// Construction does not connect; Connect() does, and the Router calls it
// again whenever the manager is not healthy().
//
// The parser checks every pair's book against Kraken's checksum. A pair that
// fails it is resubscribed on the live connection, unsubscribing and then
// subscribing again for a fresh snapshot, by a thread of the manager's own,
// so the parser never waits on the manager's lock. A pair that keeps failing
// is resubscribed at most once per second.
//
// Every public method may be called any number of times, in any order and from
// any thread; each leaves the manager either fully connected or fully
// disconnected, and each subscription either fully set up or fully torn down,
// even when it throws. Subscriptions outlive connections: they are sent again
// on every Connect(). Destruction disconnects and drops every subscription.
class VenueManager : public venue::Manager {
 public:
  VenueManager();

  VenueManager(const VenueManager&) = delete;
  VenueManager& operator=(const VenueManager&) = delete;

  ~VenueManager() override;

  std::string_view name() const override { return kVenueName; }

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

  // Starts with a snapshot. `depth` must be one of kL2Depths. Fetches the
  // pair's precision from the venue, so it blocks for a REST round trip.
  // Throws std::logic_error if `id` is in use.
  std::expected<venue::SubscriptionId, venue::Error> Subscribe(
      venue::SubscriptionId id,
      const std::string& symbol,
      u32 depth,
      venue::Sink sink,
      venue::ReceiverFactory make_receiver) override;

  // Also unsubscribes from the pair.
  std::expected<void, venue::Error> Unsubscribe(venue::SubscriptionId id,
                                                venue::Sink sink) override;

  std::vector<venue::SubscriptionInfo> List() const override;

 private:
  // The one subscriber to a pair.
  struct Subscription {
    std::string symbol;
    u32 depth;
    venue::Sink sink;
    i64 start_time_ns;
    std::unique_ptr<core::Receiver<l2::Message>> receiver;
  };

  // What Subscribe() returns when `id` or `symbol` are already taken: the
  // existing recording's id if `sink` is kRecord and the pair is recorded at
  // `depth`, or else kSymbolInUse. Nothing if the subscription may go ahead.
  // Throws std::logic_error if `id` is in use, which only a caller bug does.
  std::optional<std::expected<venue::SubscriptionId, venue::Error>>
  ResolveTakenLocked(venue::SubscriptionId id,
                     std::string_view symbol,
                     u32 depth,
                     venue::Sink sink) const;

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

  // Serializes every public method but name().
  mutable std::mutex life_cycle_mutex_;
  std::unique_ptr<Credentials> credentials_;
  WebsocketTokenGenerator signer_;
  core::PipelineStatus status_;
  net::RawFrameQueue raw_frames_queue_;
  // Declared before the parser, which points into it, so it outlives it.
  std::flat_map<venue::SubscriptionId, Subscription> subscriptions_;
  L2MessageParser parser_;
  // Declared after the parser so it is destroyed first: it feeds the parser.
  net::WebsocketFeed feed_;
  bool connected_{false};
  // Pairs waiting to be resubscribed, in the order their checksums failed.
  std::mutex resubscribe_mutex_;
  std::condition_variable_any resubscribe_wake_;
  std::vector<std::string> pending_resubscribes_;
  // Stopped first thing in the destructor.
  std::jthread resubscriber_;
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_VENUE_MANAGER_H_
