#ifndef DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
#define DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace data_feed {

class KrakenWebsocketTokenGenerator;

class KrakenWebsocketToken {
 public:
  using clock = std::chrono::high_resolution_clock;
  using timestamp = clock::time_point;

  explicit KrakenWebsocketToken(std::string token, int expires)
      : token_(std::move(token)),
        expires_(expires),
        generation_timestamp_(clock::now()) {}

  ~KrakenWebsocketToken() = default;

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

  KrakenWebsocketToken(const KrakenWebsocketToken&) = delete;
  void operator=(const KrakenWebsocketToken&) = delete;

  KrakenWebsocketToken(KrakenWebsocketToken&&) = delete;
  void operator=(KrakenWebsocketToken&&) = delete;

 private:
  std::string token_;
  int expires_;
  timestamp generation_timestamp_;
  bool connection_established_{false};
};

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_H_
