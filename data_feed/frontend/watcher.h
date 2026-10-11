#ifndef DATA_FEED_FRONTEND_WATCHER_H_
#define DATA_FEED_FRONTEND_WATCHER_H_

#include "frontend/wire.h"

namespace frontend {

// Somewhere events for one client go: a connection of the Server, or a fake
// in tests.
class Watcher {
 public:
  virtual ~Watcher() = default;

  // Queues the event carrying `payload` for the client, which receives events
  // in the order they were sent. Called from any thread, often while the
  // caller holds a lock, so it must not block. The watcher adds the
  // envelope's sequence and send time.
  virtual void Send(Payload payload) = 0;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_WATCHER_H_
