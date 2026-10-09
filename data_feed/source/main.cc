#include <chrono>
#include <csignal>
#include <exception>
#include <flat_map>
#include <fstream>
#include <ios>
#include <memory>
#include <print>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_pair_precision.h"
#include "kraken_websocket_token_generator.h"
#include "l2_feed_loop.h"
#include "l2_kraken_data_feed.h"
#include "l2_message_dispatcher.h"
#include "l2_message_writer_lane.h"
#include "pipeline_status.h"

namespace {

constexpr u64 kBookDepth = 100;

// Every raw frame from the venue is appended here, one per line.
constexpr std::string_view kOutputPath = "kraken_l2_messages.txt";

// Global so the Ctrl+C handler can reach it.
data_feed::PipelineStatus status;

}  // namespace

int main() {
  std::signal(SIGINT, [](int) { status.stop_requested = true; });

  try {
    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    const std::flat_map<std::string, u64> symbol_depth_mapping{
        {"BTC/USD", kBookDepth}, {"ETH/USD", kBookDepth}};

    const data_feed::PairPrecisionMap pair_precision =
        data_feed::FetchKrakenPairPrecision(symbol_depth_mapping |
                                            std::views::keys |
                                            std::ranges::to<std::vector>());

    const std::unique_ptr<data_feed::Level2KrakenDataFeed> feed =
        std::make_unique<data_feed::Level2KrakenDataFeed>(signer,
                                                          symbol_depth_mapping);

    // Declared before the parser so the parser joins first and every message
    // it dispatched reaches a still running lane.
    const data_feed::WriterLaneMap lanes =
        symbol_depth_mapping | std::views::keys |
        std::views::transform([](const std::string& symbol) {
          return std::pair{
              symbol, std::make_unique<data_feed::L2MessageWriterLane>(symbol)};
        }) |
        std::ranges::to<data_feed::WriterLaneMap>();

    const std::unique_ptr<data_feed::FrameQueue> frames =
        std::make_unique<data_feed::FrameQueue>();

    std::ofstream output{std::string{kOutputPath}, std::ios::app};
    if (!output) {
      std::println("Fatal: cannot open {}", kOutputPath);
      return 1;
    }

    feed->Connect();

    // Declared after the queue and lanes so it is joined before they die.
    const std::jthread parser{[&frames, &lanes,
                               &pair_precision](const std::stop_token& stop) {
      data_feed::ParseAndDispatch(stop, *frames, lanes, pair_precision, status);
    }};

    // Declared last so it joins first: the network loop must stop producing
    // frames before the parser (and its queue) are torn down.
    const std::jthread network{[&feed, &frames, &lanes, &output] {
      data_feed::RunL2FeedLoop(*feed, *frames, lanes, output, status);
    }};

    while (!status.stop_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
