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
#include "constants.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token_generator.h"
#include "l2_kraken_data_feed.h"
#include "l2_orderbook.h"
#include "l2_record.h"
#include "l2_update.h"
#include "market_data.pb.h"
#include "websocket_broadcast_server.h"

namespace {

constexpr u64 kBookDepth = 10;
constexpr std::string_view kBookSymbol = "BTC/USD";

// BTC/USD precision on Kraken: 1 price decimal, 8 quantity decimals. This is
// how double_string_to_i64 packs the decimal strings into Level2Record ints.
constexpr double kPriceScale = 10.0;
constexpr double kQuantityScale = 1e8;

std::string BuildBookTickerFrame(const data_feed::Level2Record& bid,
                                 const data_feed::Level2Record& ask) {
  trading::market_data::v1::MarketDataEvent event;
  trading::market_data::v1::BookTicker& ticker = *event.mutable_book_ticker();
  ticker.set_symbol(std::string{kBookSymbol});
  ticker.set_bid_price(static_cast<double>(bid.price) / kPriceScale);
  ticker.set_bid_qty(static_cast<double>(bid.quantity) / kQuantityScale);
  ticker.set_ask_price(static_cast<double>(ask.price) / kPriceScale);
  ticker.set_ask_qty(static_cast<double>(ask.quantity) / kQuantityScale);
  ticker.set_timestamp_ns(
      static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count()));
  return event.SerializeAsString();
}

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
  MovingAverage time_average{20uz};
  std::chrono::high_resolution_clock::time_point previous =
      std::chrono::high_resolution_clock::now();

  try {
    data_feed::WebsocketBroadcastServer frontend_stream{
        data_feed::kFrontendStreamPort};
    data_feed::Level2Orderbook orderbook{kBookDepth};

    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};

    data_feed::Level2KrakenDataFeed feed{signer, kBookDepth,
                                         std::string{kBookSymbol}};
    feed.Connect();

    while (true) {
      const data_feed::Level2Update update = feed.Next();

      orderbook.ConsumeUpdate(update);

      if (update.checksum != orderbook.CalculateChecksum()) {
        throw std::runtime_error{"Checksums do not match!"};
      }

      if (!orderbook.buy_side().empty() && !orderbook.sell_side().empty()) {
        frontend_stream.Broadcast(
            BuildBookTickerFrame(orderbook.best_bid(), orderbook.best_ask()));
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
