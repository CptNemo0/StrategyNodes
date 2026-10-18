#include "frontend/stream.h"

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

namespace {

wire::FeedStatus MakeFeedStatus(
    wire::FeedStatus::State state,
    std::string detail,
    std::span<const venue::SubscriptionId> healthy) {
  wire::FeedStatus status;
  status.set_state(state);
  status.set_detail(std::move(detail));
  status.mutable_healthy_stream_ids()->Add(healthy.begin(), healthy.end());
  return status;
}

}  // namespace

Stream::Stream(venue::SubscriptionId id,
               std::string_view venue,
               const l2::BookSpec& book,
               const l2::PairPrecision& precision)
    : id_{id},
      stream_info_{MakePayload(MakeStreamInfo(id, venue, book, precision))},
      disconnected_{MakePayload(MakeFeedStatus(
          wire::FeedStatus::STATE_DISCONNECTED,
          std::format("Lost the connection to {}; {} resumes with a snapshot "
                      "once it is back",
                      venue,
                      book.symbol),
          {}))},
      resubscribing_{MakePayload(MakeFeedStatus(
          wire::FeedStatus::STATE_DEGRADED,
          std::format("{} stopped matching {}'s checksum; it resumes with a "
                      "fresh snapshot",
                      book.symbol,
                      venue),
          {}))},
      live_{MakePayload(MakeFeedStatus(wire::FeedStatus::STATE_LIVE,
                                       std::format("{} is back", book.symbol),
                                       std::span{&id_, 1}))},
      book_{book.depth} {}

void Stream::Receive(const l2::Message& message) {
  const std::scoped_lock lock{mutex_};
  if (message.type == l2::Message::Type::kSnapshot) {
    has_snapshot_ = true;
    if (std::exchange(interrupted_, false)) {
      SendLocked(live_);
    }
  }
  book_.Apply(message);
  checksum_ = message.checksum;
  venue_time_ns_ = message.venue_time_ns;
  capture_time_ns_ = message.capture_time_ns;

  if (!watchers_.empty()) {
    SendLocked(MakePayload(MakeBook(id_, message)));
  }
}

void Stream::Interrupt(core::Gap gap) {
  const std::scoped_lock lock{mutex_};
  book_.Clear();
  has_snapshot_ = false;
  interrupted_ = true;
  gap_status_ =
      gap == core::Gap::kResubscribing ? &resubscribing_ : &disconnected_;
  SendLocked(*gap_status_);
}

void Stream::Watch(const std::shared_ptr<Watcher>& watcher) {
  const std::scoped_lock lock{mutex_};
  // Watching again starts over rather than doubling every message.
  std::erase(watchers_, watcher);

  watcher->Send(stream_info_);
  if (interrupted_) {
    watcher->Send(*gap_status_);
  } else if (has_snapshot_) {
    watcher->Send(MakePayload(SnapshotLocked()));
  }
  watchers_.push_back(watcher);
}

void Stream::Unwatch(const Watcher& watcher) {
  const std::scoped_lock lock{mutex_};
  std::erase_if(watchers_, [&watcher](const std::shared_ptr<Watcher>& w) {
    return w.get() == &watcher;
  });
}

wire::Book Stream::SnapshotLocked() const {
  wire::Book book;
  book.set_stream_id(id_);
  book.set_kind(wire::Book::KIND_SNAPSHOT);
  AppendLevels(book_.bids(), std::identity{}, *book.mutable_bids());
  AppendLevels(book_.asks(), std::identity{}, *book.mutable_asks());
  book.set_checksum(checksum_);
  book.set_venue_time_ns(venue_time_ns_);
  book.set_capture_time_ns(capture_time_ns_);
  return book;
}

void Stream::SendLocked(const Payload& payload) const {
  for (const std::shared_ptr<Watcher>& watcher : watchers_) {
    watcher->Send(payload);
  }
}

}  // namespace frontend
