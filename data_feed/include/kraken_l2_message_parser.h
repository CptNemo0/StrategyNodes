#ifndef DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
#define DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_

#include <flat_map>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include "data_level.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "message_parser.h"
#include "pipeline_status.h"
#include "raw_frame.h"
#include "receiver.h"
#include "venues.h"

namespace data_feed {

// Turns frames from Kraken's "book" channel into L2Messages on its own thread
// and hands each to the receiver attached for its pair. Other frames
// (heartbeat, status, acknowledgements) and pairs with no receiver are
// ignored. Prices and quantities are scaled to fixed-point integers using the
// precision given at Attach().
//
// Start() and Stop() may alternate any number of times. Attach() and Detach()
// are safe to call from any thread while it runs.
template <>
class MessageParser<Venue::kKraken, DataLevel::kL2> {
 public:
  // `frames` and `status` are shared with the feed and must outlive the
  // parser.
  MessageParser(RawFrameQueue& frames, PipelineStatus& status);

  MessageParser(const MessageParser&) = delete;
  MessageParser& operator=(const MessageParser&) = delete;

  ~MessageParser();

  // Starts the parser thread. Must be stopped.
  void Start();

  // Stops the parser thread once the frame queue is empty, so every frame
  // pushed before the call is parsed. Does nothing while stopped.
  void Stop();

  // Routes the messages of `symbol` to `receiver` from now on, replacing any
  // previous receiver. `receiver` must stay alive until Detach().
  void Attach(const std::string& symbol,
              const PairPrecision& precision,
              Receiver<L2Message>& receiver);

  // Ignores the messages of `symbol` from now on. Once it returns, the
  // detached receiver is never called again and may be destroyed.
  void Detach(std::string_view symbol);

 private:
  struct Route {
    PairPrecision precision;
    Receiver<L2Message>* receiver;
    // Updates are dropped until the pair's snapshot arrives, so every
    // receiver's stream starts with a snapshot.
    bool awaiting_snapshot{true};
  };

  // Parses one frame and delivers it, if it is a book frame for an attached
  // pair.
  void Dispatch(const RawFrame& frame);

  RawFrameQueue& frames_;
  PipelineStatus& status_;
  // Held for every delivery, so Detach() cannot return mid-delivery.
  std::mutex routes_mutex_;
  std::flat_map<std::string, Route, std::less<>> routes_;
  std::jthread thread_;
};

using KrakenL2MessageParser = MessageParser<Venue::kKraken, DataLevel::kL2>;

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
