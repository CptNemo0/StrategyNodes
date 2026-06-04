#ifndef DATA_FEED_SIGNER_H_
#define DATA_FEED_SIGNER_H_

#include <memory>

#include "kraken_credentials.h"
#include "kraken_websocket_token.h"

namespace data_feed {

class KrakenWebsocketTokenGenerator {
 public:
  explicit KrakenWebsocketTokenGenerator(const KrakenCredentials& credentials);

  std::unique_ptr<KrakenWebsocketToken> GenerateToken() const;

 private:
  const KrakenCredentials& credentials_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_SIGNER_H_
