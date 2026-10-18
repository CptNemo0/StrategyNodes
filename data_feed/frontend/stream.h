#ifndef DATA_FEED_FRONTEND_STREAM_H_
#define DATA_FEED_FRONTEND_STREAM_H_

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/watcher.h"
#include "frontend/wire.h"
#include "l2/book.h"
#include "l2/book_spec.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace frontend {

// The book of one live subscription and the clients watching it. Its
// Transmitter feeds it; the Outbox hands it watchers. Shared between the two,
// so the Outbox can let go of its own lock before a Watch builds a snapshot.
//
// Keeps the book as the messages leave it, truncated to its depth as the
// venue's checksum expects (or whole, at l2::kFullDepth), so that a client can start watching at any time:
// it gets a StreamInfo and a snapshot of the book, then every message after
// it. Each message is serialized once, however many clients watch.
//
// At a gap in the stream, the book is dropped, as nothing says what it does
// until the snapshot that ends the gap: watchers get a FeedStatus saying why
// (DISCONNECTED when the venue connection ended, DEGRADED when the pair is
// being resubscribed after a checksum mismatch), and a client that starts
// watching meanwhile gets it in place of a snapshot. A FeedStatus LIVE goes
// out ahead of the snapshot that ends the gap.
//
// Every method may be called from any thread.
class Stream {
 public:
  Stream(venue::SubscriptionId id,
         std::string_view venue,
         const l2::BookSpec& book,
         const l2::PairPrecision& precision);

  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;

  // Applies the message to the book and sends it to every watcher. The work
  // is bounded by the depth, or by the message at l2::kFullDepth (a full
  // Coinbase snapshot runs to tens of thousands of levels), and sending only
  // queues.
  void Receive(const l2::Message& message);

  // The stream has a gap, for the reason `gap` gives.
  void Interrupt(core::Gap gap);

  // Sends `watcher` a StreamInfo, then a snapshot of the book or a FeedStatus
  // saying there is none, then every message received until Unwatch().
  // Watching again starts over.
  void Watch(const std::shared_ptr<Watcher>& watcher);

  // Sends `watcher` nothing more once it returns.
  void Unwatch(const Watcher& watcher);

 private:
  // A snapshot of `book_` for the wire.
  wire::Book SnapshotLocked() const;

  void SendLocked(const Payload& payload) const;

  const venue::SubscriptionId id_;
  // Sent to every new watcher, or around every gap, so serialized once up
  // front.
  const Payload stream_info_;
  const Payload disconnected_;
  const Payload resubscribing_;
  const Payload live_;
  // Guards everything below, and orders every send to the watchers: a
  // watcher added mid-stream gets its snapshot strictly between two messages.
  mutable std::mutex mutex_;
  l2::Book book_;
  // The status of the gap in progress, while `interrupted_`.
  const Payload* gap_status_{nullptr};
  // False until the venue's first snapshot, and again from the end of a
  // connection until the next one's snapshot.
  bool has_snapshot_{false};
  // From the end of a connection until the next one's snapshot.
  bool interrupted_{false};
  // The checksum and times of the last message, which a snapshot built from
  // the book carries.
  u32 checksum_{0};
  i64 venue_time_ns_{0};
  i64 capture_time_ns_{0};
  std::vector<std::shared_ptr<Watcher>> watchers_;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_STREAM_H_
