#ifndef DATA_FEED_CHANNELS_ENUM_H_
#define DATA_FEED_CHANNELS_ENUM_H_

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace data_feed {

enum class Channels {
  kHeartbeat = 0,
  kStatus = 1,
  kAuction = 2,
  kMatches = 3,
  kRFQMatches = 4,
  kTicker = 5,
  kFull = 6,
  kUser = 7,
  kLevel2Batch = 8,
  kLevel3 = 9,
  kBalance = 10,
  kChannelNum = kBalance,
};

std::string_view ToString(Channels channel);

std::optional<Channels> ChannelFromString(std::string_view name);

// True for channels the Coinbase Exchange feed only serves over an
// authenticated subscription: full, user, level2(_batch), level3, rfq_matches.
bool RequiresAuthentication(Channels channel);

}  // namespace data_feed

#endif  // ! DATA_FEED_CHANNELS_ENUM_H_
