#ifndef DATA_FEED_KRAKEN_DATA_FEED_L2_H_
#define DATA_FEED_KRAKEN_DATA_FEED_L2_H_

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <flat_map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_websocket_token.h"
#include "kraken_websocket_token_generator.h"
#include "tls_context.h"

namespace data_feed {

// Owns a single TLS websocket connection to Kraken's L2 "book" channel that
// carries the books of many pairs, and hands out the raw JSON frames exactly
// as the venue sent them. The token generator
// (and the credentials behind it) is an external entity that must outlive this
// object.
class Level2KrakenDataFeed {
 public:
  // `symbol_depth_mapping` maps each pair (e.g. "BTC/USD") to its book depth,
  // which must be one of `kKrakenL2Depths`.
  Level2KrakenDataFeed(
      const KrakenWebsocketTokenGenerator& signer,
      const std::flat_map<std::string, u64>& symbol_depth_mapping);

  Level2KrakenDataFeed(const Level2KrakenDataFeed&) = delete;
  Level2KrakenDataFeed& operator=(const Level2KrakenDataFeed&) = delete;

  // Resolves the host, performs the TLS and websocket handshakes and
  // subscribes to every pair in the mapping; consumes the status frame.
  // Subscription acknowledgements are returned by `Next`.
  void Connect();

  // Adds the pair to the mapping and, when connected, subscribes to it. A pair
  // already subscribed at a different depth is resubscribed at the new one.
  void Subscribe(std::string_view symbol, u64 depth);

  // Removes the pair from the mapping and, when connected, unsubscribes from
  // it. Unknown pairs are ignored.
  void Unsubscribe(std::string_view symbol);

  // Blocks until the next frame arrives and returns it verbatim. Frames from
  // all channels (book, heartbeat, status etc.) are returned.
  std::string Next();

  void Close();

 private:
  using WebsocketStream = boost::beast::websocket::stream<
      boost::beast::ssl_stream<boost::beast::tcp_stream>>;

  const KrakenWebsocketTokenGenerator& signer_;
  std::flat_map<std::string, u64, std::less<>> symbol_depth_mapping_;

  TlsContext tls_;
  boost::asio::io_context ioc_;
  WebsocketStream ws_;
  void ReserveBuffer(std::string_view symbol, u64 depth);

  boost::beast::flat_buffer buffer_;
  std::unique_ptr<KrakenWebsocketToken> token_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_DATA_FEED_L2_H_
