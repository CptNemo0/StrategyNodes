#ifndef DATA_FEED_CONSTANTS_H_
#define DATA_FEED_CONSTANTS_H_

#include <stdint.h>

#include <string_view>

namespace data_feed {

inline constexpr std::string_view kKrakenRestEndpoint =
    "https://api.kraken.com";
inline constexpr std::string_view kKrakenTokenEndpoint =
    "/0/private/GetWebSocketsToken";

inline constexpr std::string_view kKrakenApiKeyVarName = "KRAKEN_API_KEY";
inline constexpr std::string_view kKrakenPrivateKeyVarName =
    "KRAKEN_PRIVATE_KEY";

// 15 minutes
inline constexpr double kKrakenWebsocketTokenExpirationTime = 15;

}  // namespace data_feed

#endif  // ! DATA_FEED_CONSTANTS_H_
