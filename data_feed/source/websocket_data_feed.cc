#include "websocket_data_feed.h"

#include <openssl/tls1.h>

#include <atomic>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
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
#include <boost/system/system_error.hpp>
#include <chrono>
#include <cstddef>
#include <deque>
#include <exception>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "aliasing.h"
#include "spsc_util.h"
#include "utility.h"

namespace data_feed {

namespace {

// How long Next() waits on the socket before rechecking its stop flag.
constexpr std::chrono::milliseconds kStopPollInterval{50};

// How long Close() waits for the venue to answer the close handshake.
constexpr std::chrono::seconds kCloseTimeout{1};

// How long the socket may stay silent before the connection counts as dead. A
// ping goes out at half of it, so a healthy venue always answers in time.
constexpr std::chrono::seconds kIdleTimeout{10};

// Opens `[stem]-[unix time ns].txt`. Nanoseconds keep the name unique however
// quickly connections follow one another. Throws if it cannot be opened.
std::ofstream OpenRawLog(std::string_view stem) {
  const std::string path = std::format("{}-{}.txt", stem, UnixNanosNow());
  std::ofstream raw_log{path};
  if (!raw_log) {
    throw std::runtime_error{std::format("Cannot open {}", path)};
  }
  return raw_log;
}

}  // namespace

// Every operation on the socket runs on the network thread, through this
// connection's io_context: reads in Next(), and writes posted by Send(). So the
// stream is never touched from two threads at once.
class WebsocketDataFeed::Connection {
 public:
  // Resolves the host and performs the TCP, TLS and websocket handshakes.
  Connection(TlsContext& tls, const Endpoints& endpoint)
      : ws_{ioc_, tls.native()} {
    if (SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                                 endpoint.host.c_str()) != 1) {
      throw std::runtime_error("Failed to set TLS SNI hostname");
    }

    boost::asio::ip::tcp::resolver resolver{ioc_};
    boost::beast::get_lowest_layer(ws_).connect(
        resolver.resolve(endpoint.host, endpoint.port));
    ws_.next_layer().handshake(boost::asio::ssl::stream_base::client);
    ws_.handshake(endpoint.host, endpoint.target);
    // Applies to the asynchronous reads and writes from here on: a silent
    // (half-open) connection fails the pending read instead of hanging it.
    ws_.set_option(boost::beast::websocket::stream_base::timeout{
        .handshake_timeout = kCloseTimeout,
        .idle_timeout = kIdleTimeout,
        .keep_alive_pings = true});
  }

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // Blocks until the next frame and returns it verbatim. Once `stop` is set,
  // closes the connection and returns std::nullopt, after which the
  // connection can only be destroyed. Throws if a read or write fails.
  std::optional<std::string> Next(const std::atomic<bool>& stop) {
    buffer_.clear();
    std::optional<boost::beast::error_code> read_result;
    ws_.async_read(buffer_, [&read_result](boost::beast::error_code ec,
                                           std::size_t) { read_result = ec; });
    ioc_.restart();
    while (!read_result) {
      if (stop) {
        Close();
        return std::nullopt;
      }
      // One handler at a time, so a completed read is returned at once even
      // while writes are still queued.
      ioc_.run_one_for(kStopPollInterval);
      if (write_error_) {
        throw boost::system::system_error{*write_error_};
      }
    }
    if (*read_result) {
      throw boost::system::system_error{*read_result};
    }
    return boost::beast::buffers_to_string(buffer_.data());
  }

  // Thread-safe: hands the text to the network thread, which writes it the
  // next time it runs the io_context.
  void Send(std::string text) {
    boost::asio::post(ioc_, [this, text = std::move(text)]() mutable {
      outbox_.push_back(std::move(text));
      // Otherwise a write is already in flight and will pick this one up.
      if (outbox_.size() == 1) {
        WriteFront();
      }
    });
  }

 private:
  using WebsocketStream = boost::beast::websocket::stream<
      boost::beast::ssl_stream<boost::beast::tcp_stream>>;

  // Beast allows one write in flight, so queued texts go out one by one.
  void WriteFront() {
    ws_.async_write(boost::asio::buffer(outbox_.front()),
                    [this](boost::beast::error_code ec, std::size_t) {
                      if (ec) {
                        write_error_ = ec;
                        outbox_.clear();
                        return;
                      }
                      outbox_.pop_front();
                      if (!outbox_.empty()) {
                        WriteFront();
                      }
                    });
  }

  // Performs the websocket close handshake, dropping the connection outright
  // if the venue does not answer in time. Completes every pending operation.
  void Close() {
    // Beast runs the close alongside the pending read, which then completes
    // with websocket::error::closed.
    ws_.async_close(boost::beast::websocket::close_code::normal,
                    [](boost::beast::error_code) {});
    ioc_.run_for(kCloseTimeout);
    if (!ioc_.stopped()) {
      // The venue did not finish the handshake; aborts every operation.
      boost::beast::get_lowest_layer(ws_).close();
      ioc_.run();
    }
  }

  boost::asio::io_context ioc_;
  WebsocketStream ws_;
  boost::beast::flat_buffer buffer_;
  // Network thread only.
  std::deque<std::string> outbox_;
  std::optional<boost::beast::error_code> write_error_;
};

WebsocketDataFeed::DataFeed(Endpoints endpoint,
                            std::string raw_log_stem,
                            RawFrameQueue& frames,
                            PipelineStatus& status)
    : endpoint_{std::move(endpoint)},
      raw_log_stem_{std::move(raw_log_stem)},
      frames_{frames},
      status_{status} {}

WebsocketDataFeed::~DataFeed() {
  Disconnect();
}

void WebsocketDataFeed::Connect() {
  try {
    raw_log_ = OpenRawLog(raw_log_stem_);
    connection_ = std::make_unique<Connection>(tls_, endpoint_);
    stop_ = false;
    thread_ = std::jthread{[this] { Run(); }};
  } catch (...) {
    Disconnect();
    throw;
  }
}

void WebsocketDataFeed::Disconnect() {
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  connection_.reset();
  // Only the network thread writes the log, and it has joined.
  if (raw_log_.is_open()) {
    raw_log_.close();
  }
}

void WebsocketDataFeed::Send(std::string text) {
  if (connection_) {
    connection_->Send(std::move(text));
  }
}

void WebsocketDataFeed::Run() {
  try {
    while (std::optional<std::string> frame = connection_->Next(stop_)) {
      // Stamped before the log write below, so the capture time measures when
      // the frame arrived rather than when we finished writing it out.
      const i64 capture_time_ns = UnixNanosNow();

      std::println(raw_log_, "{}", *frame);
      raw_log_.flush();
      PushBlocking(frames_, RawFrame{std::move(*frame), capture_time_ns},
                   status_.failed);
    }
  } catch (const std::exception& e) {
    // Most likely the venue dropped the connection. Escaping the thread would
    // terminate the process, so the pipeline is marked failed instead.
    std::println("Fatal in network: {}", e.what());
    status_.Fail();
  }
}

}  // namespace data_feed
