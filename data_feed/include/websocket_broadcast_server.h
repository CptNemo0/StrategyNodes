#ifndef DATA_FEED_WEBSOCKET_BROADCAST_SERVER_H_
#define DATA_FEED_WEBSOCKET_BROADCAST_SERVER_H_

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aliasing.h"

namespace data_feed {

class WebsocketBroadcastServer {
 public:
  explicit WebsocketBroadcastServer(u16 port);
  ~WebsocketBroadcastServer();

  WebsocketBroadcastServer(const WebsocketBroadcastServer&) = delete;
  WebsocketBroadcastServer& operator=(const WebsocketBroadcastServer&) = delete;

  // Queues one binary frame for delivery to every currently connected client.
  void Broadcast(std::string frame);

 private:
  class Session : public std::enable_shared_from_this<Session> {
   public:
    explicit Session(boost::asio::ip::tcp::socket socket)
        : ws_{std::move(socket)} {}

    void Start();

    bool open() const { return open_; }

    void Send(const std::shared_ptr<const std::string>& frame);

   private:
    void Read();

    void MaybeWrite();

    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer buffer_;
    std::deque<std::shared_ptr<const std::string>> queue_;
    bool handshake_done_{false};
    bool writing_{false};
    bool open_{true};
  };

  void Accept();

  boost::asio::io_context ioc_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::vector<std::weak_ptr<Session>> sessions_;

  // Declared last so joining happens before the members above are destroyed.
  std::jthread io_thread_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_WEBSOCKET_BROADCAST_SERVER_H_
