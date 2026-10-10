#ifndef DATA_FEED_KRAKEN_VENUE_MANAGER_H_
#define DATA_FEED_KRAKEN_VENUE_MANAGER_H_

#include <concepts>
#include <flat_map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/l2_message_parser.h"
#include "kraken/websocket_token_generator.h"
#include "l2/file_writer.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "net/websocket_feed.h"

namespace kraken {

// Kraken's L2 books. Owns everything needed to stream them: the credentials,
// a generic websocket feed and Kraken's L2 parser, plus the PipelineStatus
// their threads share. Wires a receiver between them for each subscription.
// Construction loads the credentials from the environment.
//
// Kraken streams a pair at one depth per connection, so a pair has at most one
// subscription: sharing it would force every receiver onto one depth.
//
// Every public method may be called any number of times, in any order and from
// any thread; each leaves the manager either fully connected or fully
// disconnected, and each subscription either fully set up or fully torn down,
// even when it throws. Subscriptions outlive connections: they are sent again
// on every Connect(). Destruction disconnects and drops every subscription.
class VenueManager {
 public:
  // Identifies a subscription across every venue, so it is chosen by the
  // caller.
  using SubscriptionId = u64;

  VenueManager();

  VenueManager(const VenueManager&) = delete;
  VenueManager& operator=(const VenueManager&) = delete;

  ~VenueManager();

  // Starts the parser, connects the feed and subscribes to every pair. Does
  // nothing while healthily connected; after a failure, tears the broken
  // connection down first. If it throws, the manager is left disconnected.
  void Connect();

  // Stops the feed, then the parser once it has parsed every frame read.
  // Subscriptions are kept. Does nothing while disconnected.
  void Disconnect();

  // Streams `symbol`'s (e.g. "BTC/USD") book into a new `ReceiverT` under
  // `id`, starting with a snapshot. `depth` must be one of kL2Depths. Throws if
  // `id` is in use or `symbol` already has a subscription. Fetches the pair's
  // precision from the venue, so it blocks for a REST round trip.
  template <typename ReceiverT>
    requires core::ReceiverOf<ReceiverT, l2::Message> &&
             std::constructible_from<ReceiverT,
                                     std::string_view,
                                     const std::string&,
                                     u32,
                                     const l2::PairPrecision&>
  void Subscribe(SubscriptionId id, const std::string& symbol, u64 depth) {
    SubscribeImpl(id, symbol, depth, [&](const l2::PairPrecision& precision) {
      return std::make_unique<ReceiverT>(kVenueName, symbol,
                                         static_cast<u32>(depth), precision);
    });
  }

  // Drops the subscription and its receiver, closing a recording, and
  // unsubscribes from its pair. Unknown ids are ignored.
  void Unsubscribe(SubscriptionId id);

  // Whether the feed or the parser died since the last Connect().
  bool failed() const { return status_.failed; }

 private:
  // The one subscriber to a pair.
  struct Subscription {
    std::string symbol;
    u64 depth;
    std::unique_ptr<core::Receiver<l2::Message>> receiver;
  };

  // Builds a subscription's receiver once the pair's precision is known.
  using ReceiverFactory =
      std::move_only_function<std::unique_ptr<core::Receiver<l2::Message>>(
          const l2::PairPrecision&)>;

  // Subscribe() for any receiver type.
  void SubscribeImpl(SubscriptionId id,
                     const std::string& symbol,
                     u64 depth,
                     ReceiverFactory make_receiver);

  // Throws if `id` is in use or `symbol` already has a subscription.
  void ThrowIfTakenLocked(SubscriptionId id, std::string_view symbol) const;

  // Disconnect() without the lock.
  void DisconnectLocked();

  // Serializes every public method but failed().
  mutable std::mutex life_cycle_mutex_;
  std::unique_ptr<Credentials> credentials_;
  WebsocketTokenGenerator signer_;
  core::PipelineStatus status_;
  net::RawFrameQueue raw_frames_queue_;
  // Declared before the parser, which points into it, so it outlives it.
  std::flat_map<SubscriptionId, Subscription> subscriptions_;
  L2MessageParser parser_;
  // Declared last so it is destroyed first: it feeds the parser.
  net::WebsocketFeed feed_;
  bool connected_{false};
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_VENUE_MANAGER_H_
