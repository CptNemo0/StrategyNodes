#ifndef DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
#define DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace kraken {

class WebsocketTokenGenerator;

class WebsocketToken {
 public:
  // Steady, so a wall-clock adjustment cannot shorten or extend the lifetime.
  using clock = std::chrono::steady_clock;
  using timestamp = clock::time_point;

  // `expires` is the lifetime in seconds, as GetWebSocketsToken reports it
  // (900, i.e. 15 minutes).
  explicit WebsocketToken(std::string token, int expires)
      : token_(std::move(token)),
        expires_(expires),
        generation_timestamp_(clock::now()) {}

  ~WebsocketToken() = default;

  std::string_view get() const { return token_; }

  bool empty() const { return token_.empty(); }

  // Once a connection is established with it, Kraken keeps that connection
  // alive past the token's lifetime.
  bool expired() const {
    return !connection_established_ && clock::now() - generation_timestamp_ >=
                                           std::chrono::seconds{expires_};
  }

  bool valid() const { return !empty() && !expired(); }

  void connection_established() { connection_established_ = true; }

  WebsocketToken(const WebsocketToken&) = delete;
  void operator=(const WebsocketToken&) = delete;

  WebsocketToken(WebsocketToken&&) = delete;
  void operator=(WebsocketToken&&) = delete;

 private:
  std::string token_;
  // Seconds.
  int expires_;
  timestamp generation_timestamp_;
  bool connection_established_{false};
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
