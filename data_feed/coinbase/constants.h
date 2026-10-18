#ifndef DATA_FEED_COINBASE_CONSTANTS_H_
#define DATA_FEED_COINBASE_CONSTANTS_H_

#include <string_view>

namespace coinbase {

// Coinbase Advanced Trade: its public market data needs no credentials. See
// https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/websocket/websocket-overview

// Recorded in every .bin file header, so a recording says which venue it came
// from without relying on where the file happens to sit.
inline constexpr std::string_view kVenueName = "coinbase";

inline constexpr std::string_view kHttpsPort = "443";

// Market data, no JWT.
inline constexpr std::string_view kWsHost = "advanced-trade-ws.coinbase.com";
inline constexpr std::string_view kWsTarget = "/";

inline constexpr std::string_view kRestHost = "api.coinbase.com";
// Public, so unsigned. One product is at kProductsEndpoint/{product_id}.
inline constexpr std::string_view kProductsEndpoint =
    "/api/v3/brokerage/market/products";

}  // namespace coinbase

#endif  // ! DATA_FEED_COINBASE_CONSTANTS_H_
