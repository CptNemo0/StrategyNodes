#ifndef DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_GENERATOR_H_
#define DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_GENERATOR_H_

#include <memory>

#include "kraken/credentials.h"
#include "kraken/websocket_token.h"

namespace kraken {

class WebsocketTokenGenerator {
 public:
  explicit WebsocketTokenGenerator(const Credentials& credentials);

  std::unique_ptr<WebsocketToken> GenerateToken() const;

 private:
  const Credentials& credentials_;
};

}  // namespace kraken

#endif  // !DATA_FEED_KRAKEN_WEBSOCKET_TOKEN_GENERATOR_H_
