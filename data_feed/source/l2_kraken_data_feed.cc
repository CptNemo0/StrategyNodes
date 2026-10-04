#include "l2_kraken_data_feed.h"

#include <openssl/tls1.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <flat_map>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "aliasing.h"
#include "constants.h"
#include "kraken_message_size_util.h"
#include "kraken_websocket_token_generator.h"

namespace data_feed {

namespace {

enum class SubscriptionMethod { kSubscribe, kUnsubscribe };

// Kraken takes a single depth per request, so all `symbols` share `depth`.
std::string BuildLevel2Message(SubscriptionMethod method,
                               u64 depth,
                               const std::vector<std::string_view>& symbols) {
  // `{::?}` renders the symbols as a JSON array of quoted strings.
  return std::format(
      R"({{"method":"{}","params":{{"channel":"book","depth":{},"symbol":{::?}}}}})",
      method == SubscriptionMethod::kSubscribe ? "subscribe" : "unsubscribe",
      depth, symbols);
}

}  // namespace

Level2KrakenDataFeed::Level2KrakenDataFeed(
    const KrakenWebsocketTokenGenerator& signer,
    const std::flat_map<std::string, u64>& symbol_depth_mapping)
    : signer_{signer},
      symbol_depth_mapping_{std::from_range, symbol_depth_mapping},
      ws_{ioc_, tls_.native()} {
  for (const auto& [symbol, depth] : symbol_depth_mapping_) {
    ReserveBuffer(symbol, depth);
  }
}

void Level2KrakenDataFeed::Connect() {
  token_ = signer_.GenerateToken();

  if (SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                               kKrakenWsL2Host.data()) != 1) {
    throw std::runtime_error("Failed to set TLS SNI hostname");
  }

  boost::asio::ip::tcp::resolver resolver{ioc_};
  boost::beast::get_lowest_layer(ws_).connect(
      resolver.resolve(kKrakenWsL2Host, kKrakenHttpsPort));
  ws_.next_layer().handshake(boost::asio::ssl::stream_base::client);
  ws_.handshake(kKrakenWsL2Host, kKrakenWsL2Target);

  // Status frame.
  buffer_.clear();
  ws_.read(buffer_);

  // One request per depth, each covering every pair mapped to it.
  for (const u64 depth : kKrakenL2Depths) {
    const std::vector<std::string_view> symbols =
        symbol_depth_mapping_ | std::views::filter([depth](const auto& entry) {
          return entry.second == depth;
        }) |
        std::views::keys | std::ranges::to<std::vector<std::string_view>>();
    if (!symbols.empty()) {
      ws_.write(boost::asio::buffer(
          BuildLevel2Message(SubscriptionMethod::kSubscribe, depth, symbols)));
    }
  }
}

void Level2KrakenDataFeed::Subscribe(std::string_view symbol, u64 depth) {
  const auto it = symbol_depth_mapping_.find(symbol);
  if (it != symbol_depth_mapping_.end()) {
    if (it->second == depth) {
      return;
    }
    // Kraken keeps one book per pair, so the old depth is dropped first.
    Unsubscribe(symbol);
  }

  symbol_depth_mapping_.emplace(symbol, depth);
  ReserveBuffer(symbol, depth);

  if (ws_.is_open()) {
    ws_.write(boost::asio::buffer(
        BuildLevel2Message(SubscriptionMethod::kSubscribe, depth, {symbol})));
  }
}

void Level2KrakenDataFeed::Unsubscribe(std::string_view symbol) {
  const auto it = symbol_depth_mapping_.find(symbol);
  if (it == symbol_depth_mapping_.end()) {
    return;
  }

  if (ws_.is_open()) {
    ws_.write(boost::asio::buffer(BuildLevel2Message(
        SubscriptionMethod::kUnsubscribe, it->second, {symbol})));
  }

  symbol_depth_mapping_.erase(it);
}

void Level2KrakenDataFeed::ReserveBuffer(std::string_view symbol, u64 depth) {
  buffer_.reserve(MaxL2SnapshotLength(depth, symbol.size()));
}

std::string Level2KrakenDataFeed::Next() {
  buffer_.clear();
  ws_.read(buffer_);
  return boost::beast::buffers_to_string(buffer_.data());
}

void Level2KrakenDataFeed::Close() {
  boost::beast::error_code ec;
  ws_.close(boost::beast::websocket::close_code::normal, ec);
}

}  // namespace data_feed
