#ifndef DATA_FEED_VENUE_ROUTER_H_
#define DATA_FEED_VENUE_ROUTER_H_

#include <atomic>
#include <chrono>
#include <expected>
#include <flat_map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/aliasing.h"
#include "venue/manager.h"

namespace venue {

// How long the Router waits before reconnecting a venue: `initial` after its
// first failed attempt, doubling after each further one up to `max`.
struct Backoff {
  std::chrono::milliseconds initial{std::chrono::seconds{1}};
  std::chrono::milliseconds max{std::chrono::seconds{30}};
};

// Routes the frontend's commands to the venue they name. Hands out
// subscription ids, unique across venues, and builds each subscription's
// receiver from its sink. Keeps every venue connected: a venue that is not
// healthy is reconnected on a background thread, waiting longer after each
// failed attempt.
//
// Every public method may be called from any thread.
class Router {
 public:
  using Managers = std::flat_map<Id, std::unique_ptr<Manager>>;

  // One subscription, with the venue it belongs to.
  struct Listing {
    Id venue;
    SubscriptionInfo subscription;
  };

  // Starts connecting every manager.
  explicit Router(Managers managers, Backoff backoff = {});

  Router(const Router&) = delete;
  Router& operator=(const Router&) = delete;

  // Stops reconnecting, then drops every venue and its subscriptions.
  ~Router();

  // Subscribes to `symbol` at `venue`, streaming into a frontend::Transmitter
  // for kLive or an l2::FileWriter for kRecord; the sink is a template
  // argument, so the receiver type is picked at compile time. Returns the
  // subscription's id, which is an existing recording's when the pair is
  // already recorded at `depth`. May block for a round trip to the venue.
  // Instantiated in router.cc for both sinks.
  template <Sink sink>
  std::expected<SubscriptionId, Error> Subscribe(Id venue,
                                                 const std::string& symbol,
                                                 u32 depth);

  // Ends the subscription `id`, which must have `sink`, so that a command
  // meant for a recording cannot stop a live feed.
  std::expected<void, Error> Unsubscribe(SubscriptionId id, Sink sink);

  // Every subscription of every venue.
  std::vector<Listing> List() const;

 private:
  // Who a subscription id belongs to.
  struct Owner {
    Id venue;
    Sink sink;
  };

  // Reconnects unhealthy managers until `stop` is requested.
  void Supervise(const std::stop_token& stop);

  Managers managers_;
  Backoff backoff_;
  // Starts at 1, so that 0 never names a subscription.
  std::atomic<SubscriptionId> next_id_{1};
  mutable std::mutex owners_mutex_;
  std::flat_map<SubscriptionId, Owner> owners_;
  // Declared last, so it is stopped before anything it uses is destroyed.
  std::jthread supervisor_;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_ROUTER_H_
