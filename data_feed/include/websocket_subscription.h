#ifndef DATA_FEED_WEBSOCKET_SUBSCRIPTION_H_
#define DATA_FEED_WEBSOCKET_SUBSCRIPTION_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "channels_enum.h"

namespace data_feed {

class Credentials;
class Signer;

// Authentication block embedded in an authenticated subscription. Required by
// the full/user/level2/level3/rfq_matches channels.
struct SubscriptionAuth {
  std::string signature;
  std::string key;
  std::string passphrase;
  std::string timestamp;
};

// An immutable, ready-to-send Coinbase Exchange subscription message. Construct
// one with WebSocketSubscriptionBuilder, then call to_string() to serialise it
// to the JSON wire format expected by the feed.
//
// Auth is held by unique_ptr (not optional) so transferring/clearing it is a
// single pointer move rather than four string moves.
class WebSocketSubscription {
 public:
  enum class Type {
    kSubscribe = 0,
    kUnsubscribe = 1,
  };

  WebSocketSubscription() = default;

  std::string to_json() const;

 private:
  friend class WebSocketSubscriptionBuilder;

  Type type_{Type::kSubscribe};
  std::vector<std::string> product_ids_;
  std::vector<Channels> channels_;
  std::unique_ptr<SubscriptionAuth> auth_;
};

// Fluent, reusable builder. It accumulates state into an owned subscription;
// Build() validates that subscription, moves it out to the caller, and installs
// a fresh one so the builder can be used again immediately.
class WebSocketSubscriptionBuilder {
 public:
  WebSocketSubscriptionBuilder();

  WebSocketSubscriptionBuilder& Type(WebSocketSubscription::Type type);
  WebSocketSubscriptionBuilder& AddProduct(std::string_view product_id);
  WebSocketSubscriptionBuilder& AddChannel(Channels channel);

  // Generates a fresh signature + timestamp via `signer` and attaches it
  // together with the key and passphrase from `credentials`.
  WebSocketSubscriptionBuilder& Authenticate(const Signer& signer,
                                             const Credentials& credentials);

  // Validates the accumulated subscription and returns ownership of it, leaving
  // the builder reset for reuse. Throws std::invalid_argument if there are no
  // channels, if a channel is listed more than once, or if an
  // authentication-only channel is requested without a prior Authenticate().
  std::unique_ptr<WebSocketSubscription> Build();

 private:
  std::unique_ptr<WebSocketSubscription> subscription_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_WEBSOCKET_SUBSCRIPTION_H_
