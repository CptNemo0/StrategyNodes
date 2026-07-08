#ifndef DATA_FEED_KRAKEN_DATA_FEED_L2_H_
#define DATA_FEED_KRAKEN_DATA_FEED_L2_H_

#include <rapidjson/document.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <memory>
#include <string>

#include "aliasing.h"
#include "kraken_websocket_token.h"
#include "kraken_websocket_token_generator.h"
#include "l2_update.h"
#include "tls_context.h"

namespace data_feed {

// Owns the TLS websocket connection to Kraken's L2 "book" channel and turns
// raw JSON frames into Level2Updates. The token generator (and the
// credentials behind it) is an external entity that must outlive this object.
class Level2KrakenDataFeed {
 public:
  Level2KrakenDataFeed(const KrakenWebsocketTokenGenerator& signer,
                       u64 depth,
                       std::string symbol);

  Level2KrakenDataFeed(const Level2KrakenDataFeed&) = delete;
  Level2KrakenDataFeed& operator=(const Level2KrakenDataFeed&) = delete;

  // Resolves the host, performs the TLS and websocket handshakes and
  // subscribes to the book channel; consumes the status and
  // subscription-acknowledgement frames.
  void Connect();

  // Blocks until the next "book" message arrives and returns it parsed.
  // Frames from other channels (heartbeat etc.) are skipped.
  Level2Update Next();

  void Close();

 private:
  using WebsocketStream = boost::beast::websocket::stream<
      boost::beast::ssl_stream<boost::beast::tcp_stream>>;

  const KrakenWebsocketTokenGenerator& signer_;
  u64 depth_;
  std::string symbol_;

  TlsContext tls_;
  boost::asio::io_context ioc_;
  WebsocketStream ws_;
  boost::beast::flat_buffer buffer_;
  rapidjson::Document document_;
  std::unique_ptr<KrakenWebsocketToken> token_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_DATA_FEED_L2_H_
