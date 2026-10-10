#ifndef DATA_FEED_CORE_RECEIVER_H_
#define DATA_FEED_CORE_RECEIVER_H_

#include <concepts>

namespace core {

// Consumes the messages of one subscription. Knows what to do with them, not
// who produces them: the venue and symbol it serves come from its constructor.
template <typename Message>
class Receiver {
 public:
  virtual ~Receiver() = default;

  // Called on the parser thread, so it must hand the message off rather than
  // process it in place.
  virtual void Receive(Message message) = 0;
};

// A concrete Receiver of `Message`s.
template <typename T, typename Message>
concept ReceiverOf = std::derived_from<T, Receiver<Message>>;

}  // namespace core

#endif  // ! DATA_FEED_CORE_RECEIVER_H_
