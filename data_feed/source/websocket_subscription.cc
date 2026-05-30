#include "websocket_subscription.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "channels_enum.h"
#include "credentials.h"
#include "signer.h"

namespace data_feed {

namespace {

template <typename T>
std::string RenderStringArray(const std::vector<T>& elements,
                              std::function<std::string(const T&)> translate) {
  std::stringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < elements.size(); ++i) {
    if (i != 0) {
      ss << ',';
    }
    ss << std::format("\"{}\"", translate(elements[i]));
  }
  ss << ']';
  return ss.str();
}

std::string_view TypeName(WebSocketSubscription::Type type) {
  switch (type) {
    case WebSocketSubscription::Type::kUnsubscribe:
      return "unsubscribe";
    case WebSocketSubscription::Type::kSubscribe:
    default:
      return "subscribe";
  }
}

}  // namespace

std::string WebSocketSubscription::to_json() const {
  std::stringstream ss;
  ss << std::format(
      R"({{"type":"{}","product_ids":{},"channels":{})", TypeName(type_),

      RenderStringArray<std::string>(
          product_ids_, [](const std::string& str) { return str; }),

      RenderStringArray<Channels>(channels_, [](const Channels& channel) {
        return ToString(channel).data();
      }));

  if (auth_ != nullptr) {
    ss << std::format(
        R"(,"signature":"{}","key":"{}","passphrase":"{}","timestamp":"{}")",
        auth_->signature, auth_->key, auth_->passphrase, auth_->timestamp);
  }

  ss << '}';
  return ss.str();
}

WebSocketSubscriptionBuilder::WebSocketSubscriptionBuilder()
    : subscription_(std::make_unique<WebSocketSubscription>()) {}

WebSocketSubscriptionBuilder& WebSocketSubscriptionBuilder::Type(
    WebSocketSubscription::Type type) {
  subscription_->type_ = type;
  return *this;
}

WebSocketSubscriptionBuilder& WebSocketSubscriptionBuilder::AddProduct(
    std::string_view product_id) {
  subscription_->product_ids_.emplace_back(product_id);
  return *this;
}

WebSocketSubscriptionBuilder& WebSocketSubscriptionBuilder::AddChannel(
    Channels channel) {
  subscription_->channels_.push_back(channel);
  return *this;
}

WebSocketSubscriptionBuilder& WebSocketSubscriptionBuilder::Authenticate(
    const Signer& signer,
    const Credentials& credentials) {
  std::unique_ptr<SubscriptionAuth> auth = std::make_unique<SubscriptionAuth>();

  Signer::Result result = signer.GenerateSignature();
  auth->signature = std::move(result.b64_signature);
  auth->timestamp = std::move(result.timestamp);

  auth->key = credentials.key();
  auth->passphrase = credentials.passphrase();

  subscription_->auth_ = std::move(auth);
  return *this;
}

std::unique_ptr<WebSocketSubscription> WebSocketSubscriptionBuilder::Build() {
  std::vector<Channels>& channels = subscription_->channels_;

  if (subscription_->channels_.empty()) {
    throw std::invalid_argument("Subscription must list at least one channel");
  }

  // Reject duplicate channels (small N — a quadratic scan is fine here).
  const auto ret = std::ranges::unique(subscription_->channels_);
  subscription_->channels_.erase(ret.begin(), ret.end());

  // Authentication-only channels cannot be requested without credentials.
  if (subscription_->auth_ == nullptr) {
    const auto search = std::ranges::find_if(channels, RequiresAuthentication);
    if (search != channels.end()) {
      throw std::invalid_argument{std::format(
          "Channel '{}' requires authentication; call Authenticate() before "
          "Build()",
          ToString(*search))};
    }
  }

  // Move the validated subscription out and reset for reuse.
  auto built = std::move(subscription_);
  subscription_ = std::make_unique<WebSocketSubscription>();
  return built;
}

}  // namespace data_feed
