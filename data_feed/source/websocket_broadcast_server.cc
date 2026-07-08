#include "websocket_broadcast_server.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "aliasing.h"
#include "constants.h"

namespace data_feed {

WebsocketBroadcastServer::WebsocketBroadcastServer(u16 port)
    : acceptor_{ioc_, boost::asio::ip::tcp::endpoint{
                          boost::asio::ip::make_address("127.0.0.1"), port}} {
  Accept();
  io_thread_ = std::jthread{[this] { ioc_.run(); }};
}

WebsocketBroadcastServer::~WebsocketBroadcastServer() {
  ioc_.stop();
}

void WebsocketBroadcastServer::Accept() {
  acceptor_.async_accept([this](const boost::beast::error_code& error,
                                boost::asio::ip::tcp::socket socket) {
    if (!error) {
      std::shared_ptr<Session> session =
          std::make_shared<Session>(std::move(socket));
      session->Start();
      sessions_.push_back(session);
    }
    Accept();
  });
}

void WebsocketBroadcastServer::Broadcast(std::string frame) {
  std::shared_ptr<const std::string> shared_frame{
      std::make_shared<const std::string>(std::move(frame))};

  // Returns true for each nullptr - a disconnected session. Sends payload and
  // returns false otherwise.
  auto send_if_alive = [shared_frame](const std::weak_ptr<Session>& weak) {
    const std::shared_ptr<Session> session = weak.lock();
    const bool dead = session == nullptr || !session->open();
    if (!dead) {
      session->Send(shared_frame);
    }
    return dead;
  };

  // Sends payload to valid sessions and erases the invalid sessions.
  auto erase_or_send = [this, send_if_alive] {
    std::erase_if(sessions_, send_if_alive);
  };

  // Calls `post` for thread-safety. `erase_or_send` lambda is handed for a
  // deferred execution on the context thread. That is also why `frame` is
  // wrapped into a shared_ptr - it must outlive the broadcast. erase_or_send is
  // a struct with 2 member fields underneath.
  // - WebsocketBroadcastServer* for this,
  // - `send_if_alive` functor instance that has only one member - a shared_ptr
  // to the `shared_frame` Basically lines 52-66 construct portable code segment
  // whose execution can be deferred to ensure thread-safety.
  boost::asio::post(ioc_, erase_or_send);
}

void WebsocketBroadcastServer::Session::Start() {
  ws_.set_option(boost::beast::websocket::stream_base::timeout::suggested(
      boost::beast::role_type::server));
  ws_.binary(true);
  ws_.async_accept(
      [self = shared_from_this()](const boost::beast::error_code& error) {
        if (error) {
          self->open_ = false;
          return;
        }
        self->handshake_done_ = true;
        self->Read();
        self->MaybeWrite();
      });
}

void WebsocketBroadcastServer::Session::Send(
    const std::shared_ptr<const std::string>& frame) {
  if (queue_.size() >= kMaxQueuedFrames) {
    queue_.erase(queue_.begin() + (writing_ ? 1 : 0));
  }
  queue_.push_back(frame);
  if (handshake_done_) {
    MaybeWrite();
  }
}

void WebsocketBroadcastServer::Session::Read() {
  ws_.async_read(buffer_, [self = shared_from_this()](
                              const boost::beast::error_code& error,
                              std::size_t /*bytes*/) {
    if (error) {
      self->open_ = false;
      return;
    }
    self->buffer_.clear();
    self->Read();
  });
}

void WebsocketBroadcastServer::Session::MaybeWrite() {
  if (writing_ || queue_.empty() || !open_) {
    return;
  }
  writing_ = true;
  ws_.async_write(
      boost::asio::buffer(*queue_.front()),
      [self = shared_from_this()](const boost::beast::error_code& error,
                                  std::size_t /*bytes*/) {
        self->writing_ = false;
        if (error) {
          self->open_ = false;
          return;
        }
        self->queue_.pop_front();
        self->MaybeWrite();
      });
}

}  // namespace data_feed
