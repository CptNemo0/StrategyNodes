#ifndef DATA_FEED_KRAKEN_VENUE_MANAGER_H_
#define DATA_FEED_KRAKEN_VENUE_MANAGER_H_

#include <flat_map>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_l2_message_parser.h"
#include "kraken_websocket_token_generator.h"
#include "l2_message.h"
#include "pipeline_status.h"
#include "raw_frame.h"
#include "receiver.h"
#include "venue_manager.h"
#include "venues.h"
#include "websocket_data_feed.h"

namespace data_feed {

// Kraken's L2 books. Owns a generic websocket feed and Kraken's L2 parser, and
// wires a receiver between them for each subscription. Construction loads the
// credentials from the environment.
//
// Every public method may be called any number of times, in any order and from
// any thread; each leaves the manager either fully connected or fully
// disconnected, and each subscription either fully set up or fully torn down,
// even when it throws. Subscriptions outlive connections: they are sent again
// on every Connect(). Destruction disconnects and drops every subscription.
template <>
class VenueManager<Venue::kKraken> {
 public:
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

  // Records `symbol`'s (e.g. "BTC/USD") book at `depth`, which must be one of
  // kKrakenL2Depths. Replaces any subscription to the pair at another depth.
  // Fetches the pair's precision from the venue, so it blocks for a REST
  // round trip.
  void Subscribe(const std::string& symbol, u64 depth);

  // Drops the pair's subscription and closes its recording. Unknown pairs are
  // ignored.
  void Unsubscribe(std::string_view symbol);

  // Whether the feed or the parser died since the last Connect().
  bool failed() const { return status_.failed; }

 private:
  // What Kraken streams for one pair, and who it goes to.
  struct Subscription {
    u64 depth;
    std::unique_ptr<Receiver<L2Message>> receiver;
  };

  // Disconnect() and Unsubscribe() without the lock.
  void DisconnectLocked();
  void UnsubscribeLocked(std::string_view symbol);

  // Serializes every public method but failed().
  mutable std::mutex life_cycle_mutex_;
  std::unique_ptr<KrakenCredentials> credentials_;
  KrakenWebsocketTokenGenerator signer_;
  PipelineStatus status_;
  RawFrameQueue raw_frames_queue_;
  // Declared before the parser, which points into them, so they outlive it.
  std::flat_map<std::string, Subscription, std::less<>> subscriptions_;
  KrakenL2MessageParser parser_;
  // Declared last so it is destroyed first: it feeds the parser.
  WebsocketDataFeed feed_;
  bool connected_{false};
};

using KrakenVenueManager = VenueManager<Venue::kKraken>;

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_VENUE_MANAGER_H_
