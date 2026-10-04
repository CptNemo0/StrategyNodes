#include <atomic>
#include <csignal>
#include <exception>
#include <flat_map>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_l2_message_parser.h"
#include "kraken_websocket_token_generator.h"
#include "l2_kraken_data_feed.h"
#include "l2_message.h"
#include "l2_message_file_writer.h"

namespace {

constexpr u64 kBookDepth = 100;

// Every raw frame from the venue is appended here, one per line.
constexpr std::string_view kOutputPath = "kraken_l2_messages.txt";

// Set on Ctrl+C so the loop exits and the .bin files get their final names.
std::atomic<bool> stop_requested{false};

using WriterMap = std::flat_map<std::string,
                                std::unique_ptr<data_feed::L2MessageFileWriter>,
                                std::less<>>;

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { stop_requested = true; });

  try {
    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    const std::flat_map<std::string, u64> symbol_depth_mapping{
        {"BTC/USD", kBookDepth}, {"ETH/USD", kBookDepth}};

    data_feed::Level2KrakenDataFeed feed{signer, symbol_depth_mapping};

    const WriterMap writers =
        symbol_depth_mapping | std::views::keys |
        std::views::transform([](const std::string& symbol) {
          return std::pair{
              symbol, std::make_unique<data_feed::L2MessageFileWriter>(symbol)};
        }) |
        std::ranges::to<WriterMap>();

    std::ofstream output{std::string{kOutputPath}, std::ios::app};
    if (!output) {
      std::println("Fatal: cannot open {}", kOutputPath);
      return 1;
    }

    feed.Connect();

    // Heartbeats arrive every second, so the flag is checked regularly.
    while (!stop_requested) {
      const std::string frame = feed.Next();
      std::println(output, "{}", frame);
      output.flush();

      const std::optional<data_feed::L2Message> message =
          data_feed::ParseKrakenL2Message(frame);
      if (!message) {
        continue;
      }

      const auto writer = writers.find(message->symbol);
      if (writer != writers.end()) {
        writer->second->Write(*message);
      }
    }

    feed.Close();
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
