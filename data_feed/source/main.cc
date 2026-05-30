#include <exception>
#include <iostream>
#include <memory>
#include <print>
#include <string>

#include "credentials.h"
#include "signer.h"
#include "tls_context.h"
#include "websocket_data_feed.h"

namespace {

// Minimal MessageHandler: prints each frame. Takes the frame by value (owning
// it), as required by the WebsocketDataFeed contract.
struct LoggingHandler {
  void OnDataReceived(std::string message) { std::println("{}", message); }
};

}  // namespace

int main() {
  try {
    const std::unique_ptr<data_feed::Credentials> credentials =
        data_feed::Credentials::FromEnvironment();
    data_feed::TlsContext tls;
    const data_feed::Signer signer{*credentials};

    // Subscribes to the authenticated "full" channel for BTC-USD and logs every
    // event until Enter is pressed. Production endpoint: the "full" channel
    // requires auth, and Exchange production keys are not valid against the
    // separate sandbox environment.
    using Feed = data_feed::WebsocketDataFeed<LoggingHandler>;
    Feed feed{"BTC-USD",         tls, signer, *credentials, LoggingHandler{},
              Feed::Endpoint::kProduction};

    std::println("Streaming BTC-USD 'full' channel. Press Enter to stop.");
    std::string line;
    std::getline(std::cin, line);

    feed.Stop();
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
