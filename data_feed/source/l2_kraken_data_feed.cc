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
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "aliasing.h"
#include "constants.h"
#include "kraken_message_size_util.h"
#include "kraken_websocket_token_generator.h"

namespace data_feed {

namespace {

std::string BuildLevel2SubscribeMessage(u64 depth, std::string_view symbol) {
  return std::format(
      R"({{"method":"subscribe","params":{{"channel":"book","depth":{},"symbol":["{}"]}}}})",
      depth, symbol);
}

}  // namespace

Level2KrakenDataFeed::Level2KrakenDataFeed(
    const KrakenWebsocketTokenGenerator& signer,
    u64 depth,
    std::string symbol)
    : signer_{signer},
      depth_{depth},
      symbol_{std::move(symbol)},
      ws_{ioc_, tls_.native()} {
  buffer_.reserve(MaxL2SnapshotLength(depth_, symbol_.size()));
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

  const std::string subscribe_message =
      BuildLevel2SubscribeMessage(depth_, symbol_);

  ws_.write(boost::asio::buffer(subscribe_message));

  buffer_.clear();
  ws_.read(buffer_);

  // Subscription acknowledgement.
  buffer_.clear();
  ws_.read(buffer_);
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
