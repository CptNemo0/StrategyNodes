#ifndef DATA_FEED_FRONTEND_OUTBOX_H_
#define DATA_FEED_FRONTEND_OUTBOX_H_

#include <flat_map>
#include <memory>
#include <mutex>

#include "frontend/stream.h"
#include "frontend/watcher.h"
#include "venue/manager.h"

namespace frontend {

// Finds the live subscription a client wants to watch. Every Transmitter
// registers its Stream here under its subscription id for as long as it
// exists, and the Server looks it up by the id a Watch names. This breaks the
// cycle between the Server, which needs the Router to build Transmitters, and
// the Transmitters, which need to reach the Server's clients.
//
// Holds its lock only to look a stream up, never while calling into one.
// Every method may be called from any thread.
class Outbox {
 public:
  Outbox() = default;

  Outbox(const Outbox&) = delete;
  Outbox& operator=(const Outbox&) = delete;

  // Called by a Transmitter as it is built. Throws std::logic_error if `id`
  // is already registered, which only a Router bug can cause.
  void Register(venue::SubscriptionId id, std::shared_ptr<Stream> stream);

  // Called by a Transmitter as it is destroyed. A Watch that found the
  // stream just before may still add its watcher, which then gets the
  // StreamInfo and snapshot but nothing after, as if the subscription had
  // ended right after it started watching.
  void Unregister(venue::SubscriptionId id);

  // Streams subscription `id`'s books to `watcher` until Unwatch(). Returns
  // false, and does nothing, if no live subscription has the id.
  bool Watch(venue::SubscriptionId id, const std::shared_ptr<Watcher>& watcher);

  // Stops streaming subscription `id`'s books to `watcher`. Once it returns,
  // the subscription sends `watcher` nothing more. Does nothing if `watcher`
  // was not watching it, or the subscription is gone.
  void Unwatch(venue::SubscriptionId id, const Watcher& watcher);

 private:
  // The stream registered under `id`, or null.
  std::shared_ptr<Stream> Find(venue::SubscriptionId id);

  std::mutex mutex_;
  std::flat_map<venue::SubscriptionId, std::shared_ptr<Stream>> streams_;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_OUTBOX_H_
