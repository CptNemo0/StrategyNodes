#ifndef DATA_FEED_CONSTANTS_H_
#define DATA_FEED_CONSTANTS_H_

#include <string_view>

namespace data_feed {

inline constexpr std::string_view kSystemName = "Stus Trading Initiative";

inline constexpr std::string_view kConfigDirectoryVarName =
    "TRADING_SYSTEM_CONFIG_DIR";

inline constexpr std::string_view kKrakenApiKeyVarName = "KRAKEN_API_KEY";
inline constexpr std::string_view kKrakenPrivateKeyVarName =
    "KRAKEN_PRIVATE_KEY";

inline constexpr std::string_view kKrakenRestEndpoint = "api.kraken.com";
inline constexpr std::string_view kKrakenTokenEndpoint =
    "/0/private/GetWebSocketsToken";

inline constexpr std::string_view kKrakenWsL3Endpoint =
    "wss://ws-l3.kraken.com/v2";

constexpr std::string_view kKrakenHttpsPort = "443";

// 15 minutes
inline constexpr double kKrakenWebsocketTokenExpirationTime = 15;

constexpr std::string_view kContentTypeJson = "application/json";

}  // namespace data_feed

#endif  // ! DATA_FEED_CONSTANTS_H_
