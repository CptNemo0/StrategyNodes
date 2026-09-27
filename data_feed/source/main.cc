#include <exception>
#include <fstream>
#include <ios>
#include <memory>
#include <ostream>
#include <print>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token_generator.h"
#include "l2_kraken_data_feed.h"

namespace {

constexpr u64 kBookDepth = 100;
constexpr std::string_view kBookSymbol = "BTC/USD";

// Every frame received from the venue is appended here, one per line.
constexpr std::string_view kOutputPath = "kraken_l2_messages.txt";

}  // namespace

int main() {
  try {
    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    data_feed::Level2KrakenDataFeed feed{signer, kBookDepth,
                                         std::string{kBookSymbol}};

    std::ofstream output{std::string{kOutputPath}, std::ios::app};
    if (!output) {
      std::println("Fatal: cannot open {}", kOutputPath);
      return 1;
    }

    feed.Connect();

    while (true) {
      // Flushed per message so nothing is lost when the process is killed.
      output << feed.Next() << std::endl;
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
