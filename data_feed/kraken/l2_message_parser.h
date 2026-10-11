#ifndef DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
#define DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_

#include <flat_map>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include "core/aliasing.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "l2/book.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace kraken {

// Turns frames from Kraken's "book" channel into l2::Messages on its own
// thread and hands each to the receiver attached for its pair. Other frames
// (heartbeat, status, acknowledgements) and pairs with no receiver are
// ignored. A book frame missing a field it needs fails the pipeline, as its
// layout is no longer the one this parser knows. Prices and quantities are
// scaled to fixed-point integers using the precision given at Attach().
//
// Given a mismatch handler, it also keeps each pair's book and checks it
// against the checksum Kraken sends with every book frame. On a mismatch the
// book can no longer be trusted, and neither can anything built from the
// frames: the frame is dropped, the receiver gets OnGap(kResubscribing), the
// pair's updates are dropped until its next snapshot, and the handler is told
// the pair, so its owner can resubscribe it for that snapshot.
//
// Start() and Stop() may alternate any number of times. Attach() and Detach()
// are safe to call from any thread while it runs.
class L2MessageParser {
 public:
  // Told a pair whose book failed its checksum. Called on the parser thread
  // while it holds its routes lock, so it must neither block nor call back
  // into the parser.
  using ChecksumMismatchHandler =
      std::move_only_function<void(std::string_view symbol)>;

  // `frames` and `status` are shared with the feed and must outlive the
  // parser. Checksums are checked only if `on_checksum_mismatch` is given.
  L2MessageParser(net::RawFrameQueue& frames,
                  core::PipelineStatus& status,
                  ChecksumMismatchHandler on_checksum_mismatch = {});

  L2MessageParser(const L2MessageParser&) = delete;
  L2MessageParser& operator=(const L2MessageParser&) = delete;

  ~L2MessageParser();

  // Starts the parser thread, for a new connection: every attached pair's
  // stream goes on from its next snapshot. Must be stopped.
  void Start();

  // Stops the parser thread once the frame queue is empty, so every frame
  // pushed before the call is parsed. Does nothing while stopped.
  void Stop();

  // Routes the messages of `symbol` to `receiver` from now on, scaled at
  // `precision`, for a subscription at `depth`, which the checksummed book is
  // kept at. Its stream starts with the pair's next snapshot. Throws if
  // `symbol` already has a receiver. `receiver` must stay alive until
  // Detach().
  void Attach(const std::string& symbol,
              const l2::PairPrecision& precision,
              u32 depth,
              core::Receiver<l2::Message>& receiver);

  // Stops routing the messages of `symbol`. Once it returns, its receiver is
  // never called again and may be destroyed. Unknown symbols are ignored.
  void Detach(std::string_view symbol);

 private:
  struct Route {
    l2::PairPrecision precision;
    // Never null.
    core::Receiver<l2::Message>* receiver;
    // Updates are dropped until the pair's snapshot arrives, so the
    // receiver's stream starts with one.
    bool awaiting_snapshot{true};
    // Checked against the venue's checksum; kept only with a mismatch
    // handler.
    l2::Book book;
  };

  // Parses one frame and delivers it, if it is a book frame for an attached
  // pair.
  void Dispatch(const net::RawFrame& frame);

  net::RawFrameQueue& frames_;
  core::PipelineStatus& status_;
  ChecksumMismatchHandler on_checksum_mismatch_;
  // Held for every delivery, so Detach() cannot return mid-delivery.
  std::mutex routes_mutex_;
  std::flat_map<std::string, Route, std::less<>> routes_;
  std::jthread thread_;
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
