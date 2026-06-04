#ifndef DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
#define DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "constants.h"

namespace data_feed {

class KrakenWebsocketTokenGenerator;

class KrakenWebsocketToken {
 public:
  using timestamp = std::chrono::high_resolution_clock::time_point;

  std::string_view get() const { return token_; }

  bool empty() const { return token_.empty(); }

  bool expired() const {
    return !connection_established_ &&
           (kKrakenWebsocketTokenExpirationTime <=
            std::chrono::duration_cast<std::chrono::minutes>(
                generation_timestamp_ -
                std::chrono::high_resolution_clock::now())
                .count());
  }

  bool valid() const { return !empty() && !expired(); }

  void connection_established() { connection_established_ = true; }

  KrakenWebsocketToken(const KrakenWebsocketToken&) = delete;
  void operator=(const KrakenWebsocketToken&) = delete;

  KrakenWebsocketToken(KrakenWebsocketToken&&) = delete;
  void operator=(KrakenWebsocketToken&&) = delete;

 private:
  friend class KrakenWebsocketTokenGenerator;

  explicit KrakenWebsocketToken(std::string token)
      : token_(std::move(token)),
        generation_timestamp_(std::chrono::high_resolution_clock::now()) {}

  std::string token_;
  timestamp generation_timestamp_;
  bool connection_established_{false};
};

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
