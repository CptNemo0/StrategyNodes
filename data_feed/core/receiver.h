#ifndef DATA_FEED_CORE_RECEIVER_H_
#define DATA_FEED_CORE_RECEIVER_H_

#include <concepts>

#include "core/aliasing.h"

namespace core {

// Why a subscription's stream has a gap.
enum class Gap : u8 {
  // The venue connection carrying it ended.
  kDisconnected = 0,
  // Its book stopped matching the venue's checksum, so it is being
  // resubscribed for a fresh snapshot.
  kResubscribing = 1,
};

// Consumes the messages of one subscription. Knows what to do with them, not
// who produces them: the venue and symbol it serves come from its constructor.
template <typename Message>
class Receiver {
 public:
  virtual ~Receiver() = default;

  // Called on the parser thread, so it must hand the message off rather than
  // process it in place.
  virtual void Receive(Message message) = 0;

  // The subscription's stream broke, for the reason `gap` gives, after every
  // message before the break was received. If the subscription goes on, its
  // next message is a snapshot; nothing in between is known. Never called
  // alongside Receive().
  virtual void OnGap(Gap gap) = 0;
};

// A concrete Receiver of `Message`s.
template <typename T, typename Message>
concept ReceiverOf = std::derived_from<T, Receiver<Message>>;

}  // namespace core

#endif  // ! DATA_FEED_CORE_RECEIVER_H_
