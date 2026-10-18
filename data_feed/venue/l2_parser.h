#ifndef DATA_FEED_VENUE_L2_PARSER_H_
#define DATA_FEED_VENUE_L2_PARSER_H_

#include <flat_map>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
#include "venue/l2_decoder.h"

namespace venue {

// Turns a venue's frames into l2::Messages on its own thread, through the
// venue's L2Decoder, and hands each to the receiver attached for its pair.
// Pairs with no receiver are ignored. A decoder that throws fails the
// pipeline.
//
// Given a ChecksumCheck, it also keeps each pair's book and checks it against
// the checksum the venue sends with every book change. On a mismatch the book
// can no longer be trusted, and neither can anything built from the change:
// it is dropped, the receiver gets OnGap(kResubscribing), the pair's updates
// are dropped until its next snapshot, and the check's handler is told the
// pair, so its owner can resubscribe it for that snapshot.
//
// Start() and Stop() may alternate any number of times. Attach() and Detach()
// are safe to call from any thread while it runs.
class L2Parser final : private L2Decoder::Output {
 public:
  // A venue's checksum over a book, as it sends it.
  using BookChecksum = u32 (*)(const l2::Book& book);

  // Told a pair whose book failed its checksum. Called on the parser thread
  // while it holds its routes lock, so it must neither block nor call back
  // into the parser.
  using ChecksumMismatchHandler =
      std::move_only_function<void(std::string_view symbol)>;

  struct ChecksumCheck {
    // Never null.
    BookChecksum checksum;
    ChecksumMismatchHandler on_mismatch;
  };

  // `frames` and `status` are shared with the feed and must outlive the
  // parser. Checksums are checked only if `checksum_check` is given.
  L2Parser(net::RawFrameQueue& frames,
           core::PipelineStatus& status,
           std::optional<ChecksumCheck> checksum_check = std::nullopt);

  L2Parser(const L2Parser&) = delete;
  L2Parser& operator=(const L2Parser&) = delete;

  ~L2Parser();

  // Starts the parser thread for a new connection, decoding with `decoder`:
  // every attached pair's stream goes on from its next snapshot. Must be
  // stopped.
  void Start(std::unique_ptr<L2Decoder> decoder);

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
    // Checked against the venue's checksum; kept only with a ChecksumCheck.
    l2::Book book;
  };

  // The route a book change of `symbol` goes to, or end() if it is dropped:
  // unsubscribed, or not yet (the venue may still send a few frames), or an
  // update before the route's first snapshot, which is left over from an
  // earlier subscription to the pair (e.g. at another depth).
  auto FindRoute(this auto& self,
                 std::string_view symbol,
                 l2::Message::Type type) {
    const auto route = self.routes_.find(symbol);
    return route != self.routes_.end() && type == l2::Message::Type::kUpdate &&
                   route->second.awaiting_snapshot
               ? self.routes_.end()
               : route;
  }

  // L2Decoder::Output, called by the decoder under routes_mutex_.
  const l2::PairPrecision* Find(std::string_view symbol,
                                l2::Message::Type type) const override;
  void Emit(l2::Message message) override;

  // Decodes one frame and delivers what it holds for attached pairs.
  void Dispatch(const net::RawFrame& frame);

  net::RawFrameQueue& frames_;
  core::PipelineStatus& status_;
  std::optional<ChecksumCheck> checksum_check_;
  // The current connection's; replaced by every Start(), used only on the
  // parser thread.
  std::unique_ptr<L2Decoder> decoder_;
  // Held for every delivery, so Detach() cannot return mid-delivery.
  std::mutex routes_mutex_;
  std::flat_map<std::string, Route, std::less<>> routes_;
  std::jthread thread_;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_L2_PARSER_H_
