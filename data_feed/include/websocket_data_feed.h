#ifndef DATA_FEED_WEBSOCKET_DATA_FEED_H_
#define DATA_FEED_WEBSOCKET_DATA_FEED_H_

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "channels_enum.h"
#include "kraken_credentials.h"
#include "kraken_websocket_token_generator.h"
#include "tls_context.h"
#include "websocket_subscription.h"

namespace data_feed {

// Compile-time policy: a handler is given ownership of each fully-reassembled
// frame as a std::string (taken by value), so it may keep the bytes past the
// call without copying again.
template <class H>
concept MessageHandler = requires(H handler, std::string message) {
  { handler.OnDataReceived(std::move(message)) };
};

// One authenticated Coinbase Exchange websocket connection for a single
// product, subscribed to the "full" channel. Owns its own io_context and a
// worker thread running a self-rearming async read loop; each completed read is
// delivered to the injected Handler (zero-overhead, inlinable).
//
// Non-copyable and non-movable: it owns an io_context and hands `this` to async
// completion handlers, so its address must be stable.
template <MessageHandler Handler>
class WebsocketDataFeed {
 public:
  enum class Endpoint { kSandbox, kProduction };

  WebsocketDataFeed(std::string_view product_id,
                    TlsContext& tls,
                    const KrakenWebsocketTokenGenerator& signer,
                    const KrakenCredentials& credentials,
                    Handler handler,
                    Endpoint endpoint = Endpoint::kSandbox)
      : product_id_(std::move(product_id)),
        handler_(std::move(handler)),
        ws_(ioc_, tls.native()) {
    namespace beast = boost::beast;
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    const Host host = HostFor(endpoint);

    // Resolve + TCP connect (asio::connect iterates the resolver results).
    asio::connect(beast::get_lowest_layer(ws_),
                  tcp::resolver{ioc_}.resolve(host.name, host.port));

    // SNI must be set before the TLS handshake (sent inside the ClientHello).
    if (SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                                 host.name.c_str()) != 1) {
      throw std::runtime_error("Failed to set TLS SNI hostname");
    }

    ws_.next_layer().handshake(asio::ssl::stream_base::client);
    ws_.handshake(host.name, "/");

    // Authenticated subscribe to the "full" channel for this product.
    const std::unique_ptr<WebSocketSubscription> subscription =
        WebSocketSubscriptionBuilder{}
            .AddProduct(product_id_)
            .AddChannel(Channels::kFull)
            .Authenticate(signer, credentials)
            .Build();
    const std::string message = subscription->to_json();
    ws_.write(asio::buffer(message));

    StartRead();
    worker_ = std::jthread([this] { ioc_.run(); });
  }

  ~WebsocketDataFeed() { Stop(); }

  WebsocketDataFeed(const WebsocketDataFeed&) = delete;
  WebsocketDataFeed& operator=(const WebsocketDataFeed&) = delete;

  // Closes the connection and joins the worker thread. Idempotent. Asks the
  // io_context thread to close the websocket; the pending async_read then
  // completes with an error, the loop stops re-arming, ioc_.run() returns, and
  // the worker is joined.
  void Stop() {
    if (stopped_.exchange(true)) {
      return;
    }
    boost::asio::post(ioc_, [this] {
      ws_.async_close(boost::beast::websocket::close_code::normal,
                      [](boost::beast::error_code) {});
    });
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  static constexpr Channels kChannel = Channels::kFull;

  struct Host {
    std::string name;
    std::string port;
  };

  static Host HostFor(Endpoint endpoint) {
    switch (endpoint) {
      case Endpoint::kProduction:
        return {"ws-direct.exchange.coinbase.com", "443"};
      case Endpoint::kSandbox:
      default:
        return {"ws-feed-public.sandbox.exchange.coinbase.com", "443"};
    }
  }

  // Arms one async read; the completion handler delivers the frame and re-arms
  // itself, forming the read loop. On error (e.g. after close) it returns
  // without re-arming, draining the io_context's work.
  void StartRead() {
    ws_.async_read(buf_, [this](boost::beast::error_code ec, std::size_t) {
      if (ec) {
        return;
      }
      // Copy the frame out of buf_ into an owned string here: buf_ is cleared
      // and reused by the next read, so the bytes must be detached now. This is
      // the single unavoidable copy; the handler and everything downstream move
      // the string from here on.
      const auto data = buf_.data();
      std::string message(static_cast<const char*>(data.data()), data.size());
      std::println("{}", message);
      buf_.clear();
      handler_.OnDataReceived(std::move(message));
      StartRead();
    });
  }

  using Stream = boost::beast::websocket::stream<
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;

  std::string product_id_;
  Handler handler_;
  boost::asio::io_context ioc_;
  Stream ws_;
  boost::beast::flat_buffer buf_;
  std::atomic<bool> stopped_{false};
  std::jthread worker_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_WEBSOCKET_DATA_FEED_H_
