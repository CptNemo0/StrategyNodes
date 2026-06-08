#include <exception>
#include <experimental/memory>
#include <memory>
#include <print>

#include "kraken_credentials.h"
#include "kraken_websocket_token.h"
#include "kraken_websocket_token_generator.h"

int main() {
  try {
    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};
    std::unique_ptr<data_feed::KrakenWebsocketToken> ptr =
        signer.GenerateToken();
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
