#include "channels_enum.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using Channels = data_feed::Channels;

inline constexpr std::array<std::pair<Channels, std::string_view>, 11>
    kChannelNames{{
        {Channels::kHeartbeat, "heartbeat"},
        {Channels::kStatus, "status"},
        {Channels::kAuction, "auction"},
        {Channels::kMatches, "matches"},
        {Channels::kRFQMatches, "rfq_matches"},
        {Channels::kTicker, "ticker"},
        {Channels::kFull, "full"},
        {Channels::kUser, "user"},
        {Channels::kLevel2Batch, "level2_batch"},
        {Channels::kLevel3, "level3"},
        {Channels::kBalance, "balance"},
    }};

}  // namespace

namespace data_feed {

std::string_view ToString(Channels channel) {
  for (const auto& [value, name] : kChannelNames) {
    if (value == channel) {
      return name;
    }
  }
  return {};
}

std::optional<Channels> ChannelFromString(std::string_view name) {
  for (const auto& [value, candidate] : kChannelNames) {
    if (candidate == name) {
      return value;
    }
  }
  return std::nullopt;
}

bool RequiresAuthentication(Channels channel) {
  switch (channel) {
    case Channels::kFull:
    case Channels::kUser:
    case Channels::kLevel2Batch:
    case Channels::kLevel3:
    case Channels::kRFQMatches:
      return true;
    default:
      return false;
  }
}
};  // namespace data_feed
