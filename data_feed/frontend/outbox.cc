#include "frontend/outbox.h"

#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "frontend/stream.h"
#include "frontend/watcher.h"
#include "venue/manager.h"

namespace frontend {

void Outbox::Register(venue::SubscriptionId id,
                      std::shared_ptr<Stream> stream) {
  const std::scoped_lock lock{mutex_};
  if (!streams_.try_emplace(id, std::move(stream)).second) {
    throw std::logic_error{
        std::format("Live subscription {} is already registered", id)};
  }
}

void Outbox::Unregister(venue::SubscriptionId id) {
  const std::scoped_lock lock{mutex_};
  streams_.erase(id);
}

bool Outbox::Watch(venue::SubscriptionId id,
                   const std::shared_ptr<Watcher>& watcher) {
  const std::shared_ptr<Stream> stream = Find(id);
  if (!stream) {
    return false;
  }
  stream->Watch(watcher);
  return true;
}

void Outbox::Unwatch(venue::SubscriptionId id, const Watcher& watcher) {
  if (const std::shared_ptr<Stream> stream = Find(id)) {
    stream->Unwatch(watcher);
  }
}

std::shared_ptr<Stream> Outbox::Find(venue::SubscriptionId id) {
  const std::scoped_lock lock{mutex_};
  const auto stream = streams_.find(id);
  return stream == streams_.end() ? nullptr : stream->second;
}

}  // namespace frontend
