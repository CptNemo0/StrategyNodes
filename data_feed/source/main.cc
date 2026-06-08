#include <exception>
#include <experimental/memory>
#include <iostream>
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
    std::cerr << "Fatal: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
