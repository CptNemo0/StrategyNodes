#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <print>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_credentials.h"
#include "kraken_data_feed_l2.h"
#include "kraken_websocket_token_generator.h"
#include "orderbook_l2.h"

namespace {

constexpr u64 kBookDepth = 10;
constexpr std::string_view kBookSymbol = "BTC/USD";

}  // namespace

class MovingAverage {
 public:
  explicit MovingAverage(u64 momentum) : momentum_{momentum} {}

  double Next(u64 record) {
    data_.push(record);
    current_sum_ += record;

    if (data_.size() > momentum_) {
      current_sum_ -= data_.front();
      data_.pop();
    }

    return current_sum_ / static_cast<double>(momentum_);
  }

 private:
  u64 momentum_;
  u64 current_sum_{};
  std::queue<u64> data_;
};

int main() {
  MovingAverage time_average{100uz};
  std::chrono::high_resolution_clock::time_point previous =
      std::chrono::high_resolution_clock::now();

  try {
    data_feed::OrderbookL2 orderbook{kBookDepth};

    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    data_feed::KrakenDataFeedL2 feed{signer, kBookDepth,
                                     std::string{kBookSymbol}};
    feed.Connect();

    auto apply_side = [&orderbook](const data_feed::Level2Records& records,
                                   data_feed::OrderbookSide side) {
      for (const data_feed::Level2Record& record : records) {
        orderbook.UpdatePriceLevel(record, side);
      }
    };

    while (true) {
      const data_feed::Level2Update update = feed.Next();

      apply_side(update.bids, data_feed::OrderbookSide::kBuy);
      apply_side(update.asks, data_feed::OrderbookSide::kSell);

      if (update.checksum != orderbook.CalculateChecksum()) {
        throw std::runtime_error{"Checksums do not match!"};
      }

      const std::chrono::high_resolution_clock::time_point now =
          std::chrono::high_resolution_clock::now();

      system("CLS");

      std::println("{}",
                   time_average.Next(
                       std::chrono::duration_cast<std::chrono::microseconds>(
                           now - previous)
                           .count()));

      previous = now;
      orderbook.Log();
    }
  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
