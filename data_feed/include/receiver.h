#ifndef DATA_FEED_RECEIVER_H_
#define DATA_FEED_RECEIVER_H_

namespace data_feed {

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

}  // namespace data_feed

#endif  // ! DATA_FEED_RECEIVER_H_
