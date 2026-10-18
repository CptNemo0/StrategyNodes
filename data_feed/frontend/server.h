#ifndef DATA_FEED_FRONTEND_SERVER_H_
#define DATA_FEED_FRONTEND_SERVER_H_

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "core/aliasing.h"
#include "frontend/outbox.h"
#include "frontend/wire.h"
#include "venue/router.h"

namespace frontend {

// The port the frontend connects to unless told otherwise.
inline constexpr u16 kStreamPort = 8765;

// Where the Server listens. Loopback by default: the frontend reaches it
// through a tunnel or reverse proxy on the same host, which terminates TLS.
struct ServerAddress {
  std::string host{"127.0.0.1"};
  // 0 lets the system pick a free port; see Server::port().
  u16 port{kStreamPort};
};

// The frontend's websocket endpoint. Each client connection sends binary
// ClientCommand frames and receives binary MarketDataEvent frames, one message
// per frame. Commands go to the Router (Start, Stop, List, ListPairs,
// ListVenues) or to the Outbox (Watch, Unwatch), and each is answered with a
// CommandResult carrying its request_id.
//
// Runs every socket on one network thread. Commands run on a small pool
// instead, as Start blocks for a round trip to the venue; each connection's
// commands run one at a time and in order, so a Watch sent after a Start sees
// its subscription. A connection that sends nothing for a while gets
// Heartbeats, and one whose client stops reading has Books dropped (its
// sequence numbers then skip, telling it to Watch again) rather than queued
// without bound.
class Server {
 public:
  // Listens on `address` at once, throwing if it cannot. `router` and
  // `outbox` must outlive the server.
  Server(venue::Router& router, Outbox& outbox, const ServerAddress& address);

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Drops every connection, then waits for every command still queued, as
  // each may already be changing a subscription.
  ~Server();

  // The port it listens on, which is the one picked when `address` gave 0.
  u16 port() const { return port_; }

 private:
  // One client connection. Defined in the .cc, as only the server uses it.
  class Session;

  // Accepts the next connection, and so on until the acceptor closes.
  void Accept();

  // Runs `command` on `session`'s command strand and sends it the result.
  void Execute(const std::shared_ptr<Session>& session,
               wire::ClientCommand command);

  // The result of `command`, from `session`, without its request_id. On the
  // session's command strand.
  wire::CommandResult Run(Session& session, const wire::ClientCommand& command);
  wire::CommandResult Start(const wire::Start& start);
  wire::CommandResult Stop(const wire::Stop& stop);
  wire::CommandResult List() const;
  wire::CommandResult Watch(Session& session, const wire::Watch& watch);
  wire::CommandResult Unwatch(Session& session);
  wire::CommandResult ListPairs(const wire::ListPairs& list_pairs);
  wire::CommandResult ListVenues() const;

  // Unwatches for a closed session and lets it go.
  void Forget(const std::shared_ptr<Session>& session);

  venue::Router& router_;
  Outbox& outbox_;
  // Declared before everything holding sessions, which own sockets on it, so
  // it is destroyed after them.
  boost::asio::io_context ioc_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard_;
  boost::asio::ip::tcp::acceptor acceptor_;
  // Delays the next accept after a failed one, which would otherwise retry in
  // a busy loop while the failure lasts (e.g. out of sockets).
  boost::asio::steady_timer accept_retry_;
  u16 port_;
  boost::asio::thread_pool commands_;
  std::mutex sessions_mutex_;
  std::set<std::shared_ptr<Session>> sessions_;
  std::jthread network_thread_;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_SERVER_H_
