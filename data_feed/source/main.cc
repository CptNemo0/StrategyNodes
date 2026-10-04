#include <cstddef>
#include <exception>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <ostream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_l2_message_parser.h"
#include "kraken_websocket_token_generator.h"
#include "l2_kraken_data_feed.h"
#include "l2_message.h"

namespace {

constexpr u64 kBookDepth = 100;
constexpr std::string_view kBookSymbol = "BTC/USD";

// Every book message is serialized and appended here, back to back.
constexpr std::string_view kOutputPath = "kraken_l2_messages.bin";

// Serialized size of the largest message: a snapshot with kBookDepth levels
// on both sides.
constexpr u64 kMaxMessageByteSize =
    data_feed::L2Message{
        .type = data_feed::L2Message::Type::kSnapshot,
        .buys = std::vector<data_feed::L2Message::Level>(kBookDepth),
        .sells = std::vector<data_feed::L2Message::Level>(kBookDepth)}
        .GetByteSize();

}  // namespace

int main() {
  try {
    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    data_feed::Level2KrakenDataFeed feed{signer, kBookDepth,
                                         std::string{kBookSymbol}};

    std::ofstream output{std::string{kOutputPath},
                         std::ios::app | std::ios::binary};
    if (!output) {
      std::println("Fatal: cannot open {}", kOutputPath);
      return 1;
    }

    std::vector<std::byte> buffer(kMaxMessageByteSize);

    feed.Connect();

    while (true) {
      const std::optional<data_feed::L2Message> message =
          data_feed::ParseKrakenL2Message(feed.Next());
      if (!message) {
        continue;
      }

      output.write(reinterpret_cast<const char*>(buffer.data()),
                   static_cast<std::streamsize>(message->Serialize(buffer)));
      // Flushed per message so nothing is lost when the process is killed.
      output.flush();
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
