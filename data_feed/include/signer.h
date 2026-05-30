#ifndef DATA_FEED_SIGNER_H_
#define DATA_FEED_SIGNER_H_

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "credentials.h"

namespace data_feed {

class Signer {
 public:
  struct Result {
    std::string b64_signature;
    std::string timestamp;
  };

  explicit Signer(const Credentials& credentials);

  // https://docs.cdp.coinbase.com/exchange/websocket-feed/authentication
  Result GenerateSignature() const;

 private:
  const Credentials& credentials_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_SIGNER_H_
