#ifndef DATA_FEED_WEBSOCKET_DATA_FEED_H_
#define DATA_FEED_WEBSOCKET_DATA_FEED_H_

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "data_feed.h"
#include "pipeline_status.h"
#include "protocol.h"
#include "raw_frame.h"
#include "tls_context.h"

namespace data_feed {

// A TLS websocket client. While connected, its network thread reads every
// frame, stamps it with its capture time, appends it to the connection's raw
// log and pushes it into the frame queue. What to subscribe to is up to the
// caller, through Send().
//
// Connect() and Disconnect() may alternate any number of times; each
// connection gets a fresh socket and its own raw log. They and Send() must
// not run concurrently with one another -- the owning VenueManager serializes
// them -- but are safe against the feed's own thread.
template <>
class DataFeed<Protocol::kWebsocket> {
 public:
  struct Endpoints {
    std::string host;
    std::string port;
    std::string target;
  };

  // Each connection logs to `[raw_log_stem]-[connect unix time ns].txt`.
  // `frames` and `status` are shared with the parser and must outlive the
  // feed.
  DataFeed(Endpoints endpoint,
           std::string raw_log_stem,
           RawFrameQueue& frames,
           PipelineStatus& status);

  DataFeed(const DataFeed&) = delete;
  DataFeed& operator=(const DataFeed&) = delete;

  ~DataFeed();

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
  PipelineStatus& status_;
  TlsContext tls_;
  // Engaged only while connected.
  std::unique_ptr<Connection> connection_;
  std::ofstream raw_log_;
  // Polled by the network thread, which must close the socket itself.
  std::atomic<bool> stop_{false};
  std::jthread thread_;
};

using WebsocketDataFeed = DataFeed<Protocol::kWebsocket>;

}  // namespace data_feed

#endif  // ! DATA_FEED_WEBSOCKET_DATA_FEED_H_
