#include "kraken/websocket_token.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace kraken {
namespace {

// Kraken's GetWebSocketsToken lifetime, in seconds.
constexpr int kFifteenMinutes = 900;

// The token text is handed back exactly as Kraken issued it.
TEST(WebsocketTokenTest, ExposesTheTokenText) {
  const WebsocketToken token{"xeAQ/RCChBYNVh53sTv1yZ5H4wIbwDF20PiHtTF+4UI",
                             kFifteenMinutes};

  EXPECT_EQ(token.get(), "xeAQ/RCChBYNVh53sTv1yZ5H4wIbwDF20PiHtTF+4UI");
  EXPECT_FALSE(token.empty());
}

// A token just issued, well within its lifetime, can be used.
TEST(WebsocketTokenTest, IsValidWhenFresh) {
  const WebsocketToken token{"token", kFifteenMinutes};

  EXPECT_FALSE(token.expired());
  EXPECT_TRUE(token.valid());
}

// An empty token is never usable, however fresh.
TEST(WebsocketTokenTest, IsNeverValidWhenEmpty) {
  const WebsocketToken token{"", kFifteenMinutes};

  EXPECT_TRUE(token.empty());
  EXPECT_FALSE(token.valid());
}

// A token issued with no lifetime is expired from the start.
TEST(WebsocketTokenTest, IsExpiredWithNoLifetimeLeft) {
  const WebsocketToken token{"token", 0};

  EXPECT_TRUE(token.expired());
  EXPECT_FALSE(token.valid());
}

// The lifetime is counted in seconds from issue: the token turns expired once
// that many seconds have passed, not before.
TEST(WebsocketTokenTest, ExpiresOnceItsLifetimeHasPassed) {
  const WebsocketToken token{"token", 1};
  EXPECT_FALSE(token.expired());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  EXPECT_TRUE(token.expired());
  EXPECT_FALSE(token.valid());
}

// Kraken keeps a connection opened with the token alive past its lifetime, so
// once one is established the token no longer expires.
TEST(WebsocketTokenTest, NoLongerExpiresOnceAConnectionIsEstablished) {
  WebsocketToken token{"token", 0};

  token.connection_established();

  EXPECT_FALSE(token.expired());
  EXPECT_TRUE(token.valid());
}

}  // namespace
}  // namespace kraken
