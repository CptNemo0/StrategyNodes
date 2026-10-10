#ifndef DATA_FEED_NET_WEBSOCKET_FEED_H_
#define DATA_FEED_NET_WEBSOCKET_FEED_H_

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "core/pipeline_status.h"
#include "net/raw_frame.h"
#include "net/tls_context.h"

namespace net {

// A TLS websocket client that hands out raw frames, stamped with their capture
// time. Knows nothing about the venue or what the frames mean -- the venue's
// manager tells it what to send, and its parser makes sense of what comes
// back. While connected, its network thread reads every frame, stamps it,
// appends it to the connection's raw log and pushes it into the frame queue.
//
// Connect() and Disconnect() may alternate any number of times; each
// connection gets a fresh socket and its own raw log. They and Send() must
// not run concurrently with one another -- the owning venue manager
// serializes them -- but are safe against the feed's own thread.
class WebsocketFeed {
 public:
  struct Endpoints {
    std::string host;
    std::string port;
    std::string target;
  };

  // Each connection logs to `[raw_log_stem]-[connect unix time ns].txt`.
  // `frames` and `status` are shared with the parser and must outlive the
  // feed.
  WebsocketFeed(Endpoints endpoint,
                std::string raw_log_stem,
                RawFrameQueue& frames,
                core::PipelineStatus& status);

  WebsocketFeed(const WebsocketFeed&) = delete;
  WebsocketFeed& operator=(const WebsocketFeed&) = delete;

  ~WebsocketFeed();

  // Opens the raw log, connects and starts the network thread. Must be
  // disconnected. If it throws, the feed is left disconnected.
  void Connect();

  // Stops the network thread, closes the connection and the raw log. Every
  // frame read before the stop is in the queue by the time it returns. Does
  // nothing while disconnected.
  void Disconnect();

  // Queues a text frame for the venue; it goes out on the network thread.
  // Ignored while disconnected.
  void Send(std::string text);

 private:
  // One socket from handshake to close. Defined in the .cc, as only the feed
  // uses it.
  class Connection;

  // Network thread body.
  void Run();

  const Endpoints endpoint_;
  const std::string raw_log_stem_;
  RawFrameQueue& frames_;
  core::PipelineStatus& status_;
  TlsContext tls_;
  // Engaged only while connected.
  std::unique_ptr<Connection> connection_;
  std::ofstream raw_log_;
  // Polled by the network thread, which must close the socket itself.
  std::atomic<bool> stop_{false};
  std::jthread thread_;
};

}  // namespace net

#endif  // ! DATA_FEED_NET_WEBSOCKET_FEED_H_
