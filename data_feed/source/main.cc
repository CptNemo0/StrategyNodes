#include <openssl/tls1.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/reader.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "aliasing.h"
#include "constants.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token.h"
#include "kraken_websocket_token_generator.h"
#include "orderbook_l2.h"
#include "tls_context.h"

namespace {

constexpr std::string_view kKrakenWsL3Host = "ws.kraken.com";
constexpr std::string_view kKrakenWsL3Target = "/v2";

std::string BuildLevel3SubscribeMessage(std::string_view token) {
  return R"({"method":"subscribe","params":{"channel":"book", "depth":10, "symbol":["BTC/USD"]}})";
}

}  // namespace

i64 double_string_to_i64(std::string_view value) {
  i64 return_value{0};
  for (char digit : value | std::ranges::views::filter(
                                [](char c) { return c >= '0' && c <= '9'; })) {
    return_value += digit - '0';
    return_value *= 10;
  }
  return return_value / 10;
}

int main() {
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  namespace ssl = boost::asio::ssl;
  using tcp = boost::asio::ip::tcp;

  try {
    std::unique_ptr<data_feed::OrderbookL2> orderbook{
        std::make_unique<data_feed::OrderbookL2>(10uz)};

    std::unique_ptr<data_feed::KrakenCredentials> credentials =
        data_feed::KrakenCredentials::FromEnvironment();
    data_feed::KrakenWebsocketTokenGenerator signer{*credentials};
    std::unique_ptr<data_feed::KrakenWebsocketToken> token =
        signer.GenerateToken();

    const std::string subscribe_message =
        BuildLevel3SubscribeMessage(token->get());

    data_feed::TlsContext tls;
    boost::asio::io_context ioc;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc,
                                                               tls.native()};
    tcp::resolver resolver{ioc};

    if (SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                 kKrakenWsL3Host.data()) != 1) {
      throw std::runtime_error("Failed to set TLS SNI hostname");
    }

    beast::get_lowest_layer(ws).connect(
        resolver.resolve(kKrakenWsL3Host, data_feed::kKrakenHttpsPort));
    ws.next_layer().handshake(ssl::stream_base::client);
    ws.handshake(kKrakenWsL3Host, kKrakenWsL3Target);

    std::println("Sending to {}{}:\n{}", kKrakenWsL3Host, kKrakenWsL3Target,
                 subscribe_message);
    ws.write(boost::asio::buffer(subscribe_message));

    beast::flat_buffer buffer;
    ws.read(buffer);
    std::println("Received:\n{}", beast::buffers_to_string(buffer.data()));

    buffer.clear();
    ws.read(buffer);

    auto update_side =
        [](data_feed::OrderbookL2* orderbook, data_feed::OrderbookSide side,
           const rapidjson::GenericValue<rapidjson::UTF8<>>& json) static {
          for (decltype(json.Size()) i{}; i < json.Size(); ++i) {
            data_feed::Level2Record record{
                double_string_to_i64(json[i]["price"].GetString()),
                double_string_to_i64(json[i]["qty"].GetString())};

            orderbook->AddRecord(record, side);
          }
        };

    while (true) {
      buffer.clear();
      ws.read(buffer);

      const std::string json =
          std::move(beast::buffers_to_string(buffer.data()));

      rapidjson::Document document;
      document.Parse<rapidjson::kParseNumbersAsStringsFlag>(json.c_str());

      if (document["channel"] != "book" || document["type"] != "update" ||
          document["channel"] != "snapshot") {
        continue;
      }

      update_side(orderbook.get(), data_feed::OrderbookSide::kBuy,
                  document["data"][0]["bids"]);
      update_side(orderbook.get(), data_feed::OrderbookSide::kSell,
                  document["data"][0]["asks"]);

      const std::string& checksum_json =
          document["data"][0]["checksum"].GetString();
      i64 checksum{};
      std::from_chars(checksum_json.data(),
                      checksum_json.data() + checksum_json.size(), checksum);
      if (checksum != static_cast<i64>(orderbook->CalculateChecksum())) {
        throw std::runtime_error{"Checksums do not match!"};
      }

      system("CLS");
      orderbook->Log();
    }

    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);

  } catch (const std::exception& e) {
    std::println("Fatal: {}", e.what());
    return 1;
  }

  return 0;
}
