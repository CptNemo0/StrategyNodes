#ifndef DATA_FEED_DATA_TYPES_H_
#define DATA_FEED_DATA_TYPES_H_

namespace data_feed {

enum class MessageType {
  kSubscribe = 0,
  kUnsubscribe = 1,
  kSubscriptions = 2,
  kOpen = 3,
  kHeartbeat = 4,
  kStatus = 5,
  kAuction = 6,

};

struct Message {};

}; // namespace data_feed

#endif // !DATA_FEED_DATA_TYPES_H_
