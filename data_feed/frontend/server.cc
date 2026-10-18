#include "frontend/server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <type_traits>
#include <utility>

#include "core/aliasing.h"
#include "core/debug_log.h"
#include "core/unix_time.h"
#include "frontend/outbox.h"
#include "frontend/watcher.h"
#include "frontend/wire.h"
#include "l2/book_spec.h"
#include "venue/manager.h"
#include "venue/router.h"

namespace frontend {

namespace {

// Threads running commands. Start blocks for a round trip to the venue, so
// more than one keeps a slow venue from holding up every other client.
constexpr std::size_t kCommandThreads = 4;

// A ClientCommand is a few dozen bytes; anything far larger is not one.
constexpr std::size_t kMaxCommandBytes = 64 * 1024;

// Frames a connection may have waiting to go out before it drops Books. A
// depth 1000 snapshot is around 20 KB, so this bounds a stalled client's
// backlog to some tens of megabytes.
constexpr std::size_t kMaxQueuedFrames = 4096;

// Frames past which a connection is dropped instead. Only frames that are
// never dropped get there, e.g. the replies to a client that keeps sending
// commands but never reads, which would otherwise queue without bound.
constexpr std::size_t kMaxBacklogFrames = 2 * kMaxQueuedFrames;

// Commands a connection may have read but not yet answered before it is
// dropped. Reading goes on while commands run, and a Start may take a round
// trip to the venue, so a client sending them faster than they run would
// otherwise queue work, and venue requests, without bound.
constexpr std::size_t kMaxPendingCommands = 64;

// How long to wait before accepting again after a failed accept.
constexpr std::chrono::milliseconds kAcceptRetryDelay{100};

// Windows lets a second socket bind a port its first holder set SO_REUSEADDR
// on, so a second data_feed would share the port rather than fail to listen.
#ifdef _WIN32
constexpr bool kReuseAddress = false;
#else
constexpr bool kReuseAddress = true;
#endif

// How long a connection may send nothing before it sends a Heartbeat.
constexpr std::chrono::seconds kHeartbeatPeriod{5};

// How long the client may stay silent, pings included, before its
// connection counts as dead. Pings go out at half of it.
constexpr std::chrono::seconds kIdleTimeout{30};

constexpr std::chrono::seconds kHandshakeTimeout{10};

wire::CommandResult Failure(wire::CommandError::Code code, std::string detail) {
  wire::CommandResult result;
  wire::CommandError& error = *result.mutable_error();
  error.set_code(code);
  error.set_detail(std::move(detail));
  return result;
}

wire::CommandResult Ok() {
  wire::CommandResult result;
  result.mutable_ok();
  return result;
}

// Every Heartbeat is the same, so it is serialized once.
const Payload& HeartbeatPayload() {
  static const Payload heartbeat = MakePayload(wire::Heartbeat{});
  return heartbeat;
}

// `outcome`'s error, or else what `on_success` makes of its value.
template <typename T, typename OnSuccess>
wire::CommandResult ToResult(const std::expected<T, venue::Error>& outcome,
                             OnSuccess&& on_success) {
  if (!outcome) {
    wire::CommandResult result;
    *result.mutable_error() = MakeCommandError(outcome.error());
    return result;
  }
  if constexpr (std::is_void_v<T>) {
    return on_success();
  } else {
    return on_success(*outcome);
  }
}

}  // namespace

// Every socket operation runs on the network thread, so the stream needs no
// lock; Send() posts there from whichever thread calls it. Its command state
// (watched_id) is touched only on its command strand.
class Server::Session : public Watcher,
                        public std::enable_shared_from_this<Session> {
 public:
  using CommandStrand =
      boost::asio::strand<boost::asio::thread_pool::executor_type>;

  Session(Server& server, boost::asio::ip::tcp::socket socket)
      : server_{server},
        ws_{std::move(socket)},
        commands_{boost::asio::make_strand(server.commands_)},
        heartbeat_{ws_.get_executor()} {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  ~Session() override = default;

  // Performs the websocket handshake, then reads commands until the
  // connection closes. On the network thread.
  void Begin() {
    ws_.set_option(boost::beast::websocket::stream_base::timeout{
        .handshake_timeout = kHandshakeTimeout,
        .idle_timeout = kIdleTimeout,
        .keep_alive_pings = true});
    ws_.read_message_max(kMaxCommandBytes);
    ws_.binary(true);
    ws_.async_accept([self = shared_from_this()](boost::beast::error_code ec) {
      // A handshake that completed just before Close() still runs; arming
      // the heartbeat now would keep the closed session, and a shutting down
      // network thread, waiting out a whole period.
      if (ec || self->closed_) {
        self->Close();
        return;
      }
      DEBUG_LOG("Session {} opened", static_cast<const void*>(self.get()));
      self->Read();
      self->ArmHeartbeat();
    });
  }

  void Send(Payload payload) override {
    boost::asio::post(
        ws_.get_executor(),
        [self = shared_from_this(), payload = std::move(payload)]() mutable {
          self->Enqueue(std::move(payload));
        });
  }

  CommandStrand& commands() { return commands_; }

  // Called once a command read from this connection has been answered.
  void CommandDone() { --pending_commands_; }

  // Drops the connection, failing whatever is pending on it. Idempotent. On
  // the network thread.
  void Close() {
    if (closed_) {
      return;
    }
    closed_ = true;
    DEBUG_LOG("Session {} closed", static_cast<const void*>(this));
    heartbeat_.cancel();
    // Not a close handshake: the client is gone, misbehaving, or the server
    // is shutting down. The queued frames stay until the pending write
    // fails, as it still points into the first.
    boost::beast::error_code ignored;
    boost::beast::get_lowest_layer(ws_).socket().close(ignored);
    server_.Forget(shared_from_this());
  }

  // The live subscription this client watches, 0 for none. Command strand
  // only.
  venue::SubscriptionId watched_id{0};

 private:
  using WebsocketStream =
      boost::beast::websocket::stream<boost::beast::tcp_stream>;

  void Read() {
    ws_.async_read(buffer_, [self = shared_from_this()](
                                boost::beast::error_code ec, std::size_t) {
      // A read that completed just before Close() still runs, but its command
      // would land behind the Forget() and leave the session watching.
      if (ec || self->closed_) {
        self->Close();
        return;
      }
      if (self->pending_commands_ >= kMaxPendingCommands) {
        DEBUG_LOG("Session {} has {} commands pending, dropping it",
                  static_cast<const void*>(self.get()), kMaxPendingCommands);
        self->Close();
        return;
      }
      ++self->pending_commands_;
      wire::ClientCommand command;
      const bool parsed = command.ParseFromArray(
          self->buffer_.data().data(), static_cast<int>(self->buffer_.size()));
      self->buffer_.clear();
      if (parsed) {
        self->server_.Execute(self, std::move(command));
      } else {
        // Through the command strand, so it cannot overtake the results of
        // commands read before it.
        boost::asio::post(self->commands_, [self] {
          self->Send(
              MakePayload(Failure(wire::CommandError::CODE_INVALID_COMMAND,
                                  "Not a ClientCommand")));
          self->CommandDone();
        });
      }
      self->Read();
    });
  }

  // Queues `payload` to go out under this connection's next sequence number.
  // On the network thread.
  void Enqueue(Payload payload) {
    if (closed_) {
      return;
    }
    if (outgoing_.size() >= kMaxQueuedFrames) {
      if (payload.field_number == wire::MarketDataEvent::kBookFieldNumber) {
        // Takes a sequence number all the same, so the client sees the gap.
        ++next_sequence_;
        return;
      }
      if (outgoing_.size() >= kMaxBacklogFrames) {
        DEBUG_LOG("Session {} has {} frames queued, dropping it",
                  static_cast<const void*>(this), kMaxBacklogFrames);
        Close();
        return;
      }
    }
    outgoing_.push_back(
        Outgoing{.sequence = next_sequence_++, .payload = std::move(payload)});
    last_enqueue_ = std::chrono::steady_clock::now();
    // Otherwise a write is in flight and picks this one up.
    if (outgoing_.size() == 1) {
      WriteFront();
    }
  }

  // Beast allows one write in flight, so frames go out one by one. The
  // envelope is written as the frame goes out, so its send time is when it
  // went on the wire, and the payload's bytes go out shared, not copied.
  void WriteFront() {
    Outgoing& front = outgoing_.front();
    front.header =
        FrameHeader(front.sequence, core::UnixNanosNow(), front.payload);
    ws_.async_write(
        std::array<boost::asio::const_buffer, 2>{
            boost::asio::buffer(front.header),
            boost::asio::buffer(*front.payload.bytes)},
        [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
          if (ec) {
            self->Close();
            return;
          }
          self->outgoing_.pop_front();
          if (!self->closed_ && !self->outgoing_.empty()) {
            self->WriteFront();
          }
        });
  }

  void ArmHeartbeat() {
    heartbeat_.expires_after(kHeartbeatPeriod);
    heartbeat_.async_wait(
        [self = shared_from_this()](boost::beast::error_code ec) {
          if (ec || self->closed_) {
            return;
          }
          if (std::chrono::steady_clock::now() - self->last_enqueue_ >=
              kHeartbeatPeriod) {
            self->Enqueue(HeartbeatPayload());
          }
          // Enqueue() may have dropped the connection.
          if (!self->closed_) {
            self->ArmHeartbeat();
          }
        });
  }

  // A frame waiting to go out.
  struct Outgoing {
    u64 sequence;
    Payload payload;
    // The envelope, written by WriteFront() as the frame goes out.
    std::string header;
  };

  Server& server_;
  WebsocketStream ws_;
  CommandStrand commands_;
  boost::asio::steady_timer heartbeat_;
  boost::beast::flat_buffer buffer_;
  // Frames waiting to go out; the front one may be in flight, so a deque,
  // whose push_back() leaves it in place.
  std::deque<Outgoing> outgoing_;
  u64 next_sequence_{0};
  std::chrono::steady_clock::time_point last_enqueue_{
      std::chrono::steady_clock::now()};
  bool closed_{false};
  // Read on the network thread, answered on the command strand.
  std::atomic<std::size_t> pending_commands_{0};
};

Server::Server(venue::Router& router,
               Outbox& outbox,
               const ServerAddress& address)
    : router_{router},
      outbox_{outbox},
      work_guard_{ioc_.get_executor()},
      acceptor_{ioc_,
                boost::asio::ip::tcp::endpoint{
                    boost::asio::ip::make_address(address.host), address.port},
                kReuseAddress},
      accept_retry_{ioc_},
      port_{acceptor_.local_endpoint().port()},
      commands_{kCommandThreads} {
  Accept();
  network_thread_ = std::jthread{[this] {
    // A handler that throws would otherwise end the thread, and the process
    // with it. Returns normally once stopped.
    while (true) {
      try {
        ioc_.run();
        return;
      } catch (const std::exception& e) {
        std::println(stderr, "Frontend server: {}", e.what());
      }
    }
  }};
}

Server::~Server() {
  // Every session must die while both the network thread's context and the
  // command pool are still alive, since it holds a socket on one and a strand
  // of the other. So the server winds down in stages rather than abandoning
  // pending work to the destructors.
  //
  // First the network thread drops every connection. Each one's pending
  // operations fail, and each queues a Forget() on its command strand, which
  // unwatches it, so the transmitters, which outlive the server, let go too.
  boost::asio::post(ioc_, [this] {
    acceptor_.close();
    accept_retry_.cancel();
    const std::scoped_lock lock{sessions_mutex_};
    for (const std::shared_ptr<Session>& session : sessions_) {
      session->Close();
    }
  });
  work_guard_.reset();
  network_thread_.join();
  // Then every command still queued runs, the Forget()s among them; a Start
  // may take a round trip to the venue.
  commands_.join();
  // Last, the replies those commands sent after the network thread ended are
  // dropped, releasing the sessions they hold.
  ioc_.restart();
  ioc_.run();
}

void Server::Accept() {
  acceptor_.async_accept([this](boost::beast::error_code ec,
                                boost::asio::ip::tcp::socket socket) {
    // Closed by the destructor, possibly after this accept completed: a
    // session started now would never be closed, and the network thread
    // would wait on it forever.
    if (ec == boost::asio::error::operation_aborted || !acceptor_.is_open()) {
      return;
    }
    if (ec) {
      std::println(stderr, "Frontend server cannot accept: {}", ec.message());
      accept_retry_.expires_after(kAcceptRetryDelay);
      accept_retry_.async_wait([this](boost::beast::error_code wait_ec) {
        if (!wait_ec && acceptor_.is_open()) {
          Accept();
        }
      });
      return;
    }
    const std::shared_ptr<Session> session =
        std::make_shared<Session>(*this, std::move(socket));
    {
      const std::scoped_lock lock{sessions_mutex_};
      sessions_.insert(session);
    }
    session->Begin();
    Accept();
  });
}

void Server::Execute(const std::shared_ptr<Session>& session,
                     wire::ClientCommand command) {
  boost::asio::post(session->commands(), [this, session,
                                          command = std::move(command)] {
    wire::CommandResult result = Run(*session, command);
    DEBUG_LOG("Session {}: {} -> {}", static_cast<const void*>(session.get()),
              command.ShortDebugString(),
              result.has_error() ? result.error().detail() : "ok");
    result.set_request_id(command.request_id());
    session->Send(MakePayload(result));
    session->CommandDone();
  });
}

wire::CommandResult Server::Run(Session& session,
                                const wire::ClientCommand& command) {
  try {
    switch (command.command_case()) {
      case wire::ClientCommand::kStart:
        return Start(command.start());
      case wire::ClientCommand::kStop:
        return Stop(command.stop());
      case wire::ClientCommand::kList:
        return List();
      case wire::ClientCommand::kWatch:
        return Watch(session, command.watch());
      case wire::ClientCommand::kUnwatch:
        return Unwatch(session);
      case wire::ClientCommand::kListPairs:
        return ListPairs(command.list_pairs());
      case wire::ClientCommand::COMMAND_NOT_SET:
        break;
    }
    return Failure(wire::CommandError::CODE_INVALID_COMMAND,
                   "No command, or one this backend does not know");
  } catch (const std::exception& e) {
    // E.g. a recording whose file cannot be opened.
    return Failure(wire::CommandError::CODE_VENUE_FAILED, e.what());
  }
}

wire::CommandResult Server::Start(const wire::Start& start) {
  const std::optional<venue::Id> venue = FromWire(start.venue());
  const std::optional<venue::Sink> sink = FromWire(start.sink());
  if (!venue || !sink) {
    return Failure(wire::CommandError::CODE_INVALID_COMMAND,
                   "Start needs a known venue and sink");
  }
  l2::BookSpec book{.symbol = start.symbol(), .depth = start.depth()};
  return ToResult(
      *sink == venue::Sink::kRecord
          ? router_.Subscribe<venue::Sink::kRecord>(*venue, std::move(book))
          : router_.Subscribe<venue::Sink::kLive>(*venue, std::move(book)),
      [](venue::SubscriptionId id) {
        wire::CommandResult result;
        result.set_started_id(id);
        return result;
      });
}

wire::CommandResult Server::Stop(const wire::Stop& stop) {
  const std::optional<venue::Sink> sink = FromWire(stop.sink());
  if (!sink) {
    return Failure(wire::CommandError::CODE_INVALID_COMMAND,
                   "Stop needs a known sink");
  }
  // The id carries its sink, so a Stop naming another one came from a
  // frontend that mixed up its ids; refused so a command meant for a
  // recording cannot stop a live feed.
  if (venue::SinkOf(stop.id()) != *sink) {
    std::println(stderr, "Frontend server: Stop of {} names {}, not its sink",
                 stop.id(), venue::SinkName(*sink));
    return Failure(wire::CommandError::CODE_SINK_MISMATCH,
                   std::format("Subscription {} is not {}", stop.id(),
                               venue::SinkName(*sink)));
  }
  return ToResult(router_.Unsubscribe(stop.id()), [] { return Ok(); });
}

wire::CommandResult Server::List() const {
  wire::CommandResult result;
  google::protobuf::RepeatedPtrField<wire::Subscription>& subscriptions =
      *result.mutable_subscriptions()->mutable_subscriptions();
  for (const venue::Router::Listing& listing : router_.List()) {
    wire::Subscription& added = *subscriptions.Add();
    added.set_id(listing.subscription.id);
    added.set_venue(ToWire(listing.venue));
    added.set_symbol(listing.subscription.book.symbol);
    added.set_depth(listing.subscription.book.depth);
    added.set_sink(ToWire(venue::SinkOf(listing.subscription.id)));
    added.set_start_time_ns(listing.subscription.start_time_ns);
  }
  return result;
}

wire::CommandResult Server::Watch(Session& session, const wire::Watch& watch) {
  if (!outbox_.Watch(watch.id(), session.shared_from_this())) {
    // Tells a recording apart from a typo. Checks the sink, as a live
    // subscription under the id may have started since the Outbox was asked.
    return std::ranges::any_of(
               router_.List(),
               [&watch](const venue::Router::Listing& listing) {
                 return listing.subscription.id == watch.id() &&
                        venue::SinkOf(listing.subscription.id) ==
                            venue::Sink::kRecord;
               })
               ? Failure(wire::CommandError::CODE_SINK_MISMATCH,
                         std::format("Subscription {} is a recording, not a "
                                     "live feed",
                                     watch.id()))
               : Failure(wire::CommandError::CODE_UNKNOWN_ID,
                         std::format("No live subscription {}", watch.id()));
  }
  // Watching a subscription replaces the one watched before.
  if (session.watched_id != 0 && session.watched_id != watch.id()) {
    outbox_.Unwatch(session.watched_id, session);
  }
  session.watched_id = watch.id();
  return Ok();
}

wire::CommandResult Server::Unwatch(Session& session) {
  if (session.watched_id != 0) {
    outbox_.Unwatch(std::exchange(session.watched_id, 0), session);
  }
  return Ok();
}

wire::CommandResult Server::ListPairs(const wire::ListPairs& list_pairs) {
  const std::optional<venue::Id> venue = FromWire(list_pairs.venue());
  if (!venue) {
    return Failure(wire::CommandError::CODE_INVALID_COMMAND,
                   "ListPairs needs a known venue");
  }
  return ToResult(router_.ListPairs(*venue), [](const venue::PairList& pairs) {
    wire::CommandResult result;
    wire::PairList& listed = *result.mutable_pairs();
    listed.mutable_pairs()->Reserve(static_cast<int>(pairs.symbols.size()));
    for (const std::string& symbol : pairs.symbols) {
      listed.add_pairs()->set_symbol(symbol);
    }
    listed.mutable_depths()->Add(pairs.depths.begin(), pairs.depths.end());
    return result;
  });
}

void Server::Forget(const std::shared_ptr<Session>& session) {
  // On the command strand, behind any command still queued, which might
  // otherwise watch again after this.
  boost::asio::post(session->commands(), [this, session] {
    if (session->watched_id != 0) {
      outbox_.Unwatch(std::exchange(session->watched_id, 0), *session);
    }
    const std::scoped_lock lock{sessions_mutex_};
    sessions_.erase(session);
  });
}

}  // namespace frontend
