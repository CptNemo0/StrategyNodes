#include "kraken_data_feed_l2.h"

#include <openssl/tls1.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/reader.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <charconv>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "aliasing.h"
#include "constants.h"
#include "kraken_websocket_token_generator.h"
#include "orderbook_l2.h"
#include "utility.h"

namespace data_feed {

namespace {

using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>>;

std::string BuildLevel2SubscribeMessage(u64 depth, std::string_view symbol) {
  return std::format(
      R"({{"method":"subscribe","params":{{"channel":"book","depth":{},"symbol":["{}"]}}}})",
      depth, symbol);
}

Level2Records ParseSide(const JsonValue& side) {
  Level2Records records;
  records.reserve(side.Size());

  for (decltype(side.Size()) i{}; i < side.Size(); ++i) {
    records.emplace_back(double_string_to_i64(side[i]["price"].GetString()),
                         double_string_to_i64(side[i]["qty"].GetString()));
  }

  return records;
}

u32 ParseChecksum(std::string_view checksum) {
  u32 value{};
  std::from_chars(checksum.data(), checksum.data() + checksum.size(), value);
  return value;
}

}  // namespace

KrakenDataFeedL2::KrakenDataFeedL2(const KrakenWebsocketTokenGenerator& signer,
                                   u64 depth,
                                   std::string symbol)
    : signer_{signer},
      depth_{depth},
      symbol_{std::move(symbol)},
      ws_{ioc_, tls_.native()} {}

void KrakenDataFeedL2::Connect() {
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
  std::println("Sending to {}{}:\n{}", kKrakenWsL2Host, kKrakenWsL2Target,
               subscribe_message);
  ws_.write(boost::asio::buffer(subscribe_message));

  buffer_.clear();
  ws_.read(buffer_);
  std::println("Received:\n{}",
               boost::beast::buffers_to_string(buffer_.data()));

  // Subscription acknowledgement.
  buffer_.clear();
  ws_.read(buffer_);
}

Level2Update KrakenDataFeedL2::Next() {
  while (true) {
    buffer_.clear();
    ws_.read(buffer_);

    const std::string json = boost::beast::buffers_to_string(buffer_.data());
    document_.Parse<rapidjson::kParseNumbersAsStringsFlag>(json.c_str());

    if (document_["channel"] != "book") {
      continue;
    }

    const JsonValue& data = document_["data"][0];

    return Level2Update{
        .bids = ParseSide(data["bids"]),
        .asks = ParseSide(data["asks"]),
        .checksum = ParseChecksum(data["checksum"].GetString())};
  }
}

void KrakenDataFeedL2::Close() {
  boost::beast::error_code ec;
  ws_.close(boost::beast::websocket::close_code::normal, ec);
}

}  // namespace data_feed
