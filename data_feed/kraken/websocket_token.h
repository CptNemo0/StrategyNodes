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
  using clock = std::chrono::high_resolution_clock;
  using timestamp = clock::time_point;

  explicit WebsocketToken(std::string token, int expires)
      : token_(std::move(token)),
        expires_(expires),
        generation_timestamp_(clock::now()) {}

  ~WebsocketToken() = default;

  std::string_view get() const { return token_; }

  bool empty() const { return token_.empty(); }

  bool expired() const {
    return !connection_established_ &&
           (expires_ <= std::chrono::duration_cast<std::chrono::minutes>(
                            generation_timestamp_ - clock::now())
                            .count());
  }

  bool valid() const { return !empty() && !expired(); }

  void connection_established() { connection_established_ = true; }

  WebsocketToken(const WebsocketToken&) = delete;
  void operator=(const WebsocketToken&) = delete;

  WebsocketToken(WebsocketToken&&) = delete;
  void operator=(WebsocketToken&&) = delete;

 private:
  std::string token_;
  int expires_;
  timestamp generation_timestamp_;
  bool connection_established_{false};
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
