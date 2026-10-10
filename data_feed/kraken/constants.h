#ifndef DATA_FEED_KRAKEN_CONSTANTS_H_
#define DATA_FEED_KRAKEN_CONSTANTS_H_

#include <string_view>

namespace kraken {

// Recorded in every .bin file header, so a recording says which venue it came
// from without relying on where the file happens to sit.
inline constexpr std::string_view kVenueName = "kraken";

inline constexpr std::string_view kApiKeyVarName = "KRAKEN_API_KEY";
inline constexpr std::string_view kPrivateKeyVarName = "KRAKEN_PRIVATE_KEY";

inline constexpr std::string_view kRestEndpoint = "api.kraken.com";
inline constexpr std::string_view kTokenEndpoint =
    "/0/private/GetWebSocketsToken";
inline constexpr std::string_view kAssetPairsEndpoint = "/0/public/AssetPairs";

inline constexpr std::string_view kWsL3Endpoint = "wss://ws-l3.kraken.com/v2";

inline constexpr std::string_view kHttpsPort = "443";

inline constexpr std::string_view kWsL2Host = "ws.kraken.com";

inline constexpr std::string_view kWsL2Target = "/v2";

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_CONSTANTS_H_
