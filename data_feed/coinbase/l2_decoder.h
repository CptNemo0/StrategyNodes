#ifndef DATA_FEED_COINBASE_L2_DECODER_H_
#define DATA_FEED_COINBASE_L2_DECODER_H_

#include <optional>
#include <string_view>

#include "core/aliasing.h"
#include "net/raw_frame.h"
#include "venue/l2_decoder.h"

namespace coinbase {

// Decodes frames of Coinbase Advanced Trade's "level2" channel, which come
// on channel "l2_data": each event of a frame is one book change, for the
// product it names, with every level's absolute quantity ("0" removes it).
// Coinbase sends no checksum, so the stream is checked otherwise, and any
// failure throws, which fails the pipeline and rebuilds the connection with
// fresh snapshots of every product:
//
// - Every frame's sequence_num must follow the last one. The number is
//   treated as the connection's, not a product's, so a gap is one for every
//   product. A number at or below the last one is out of order and ignored.
// - The "heartbeats" channel's heartbeat_counter must not skip.
// - A level's side is "bid" or "offer", its quantity is not negative, and
//   its price and quantity have no more fraction digits than the product's
//   precision, which would mean the increments changed.
//
// "error" frames are logged; other frames (subscriptions, other channels) are
// ignored.
class L2Decoder final : public venue::L2Decoder {
 public:
  void Decode(const net::RawFrame& frame, Output& output) override;

 private:
  // Whether `next` follows `last`, which it then becomes. False if it is out
  // of order. Throws if it skips past last + 1, naming `what` skipped.
  static bool Advance(std::optional<u64>& last,
                      u64 next,
                      std::string_view what);

  std::optional<u64> last_sequence_;
  std::optional<u64> last_heartbeat_;
};

}  // namespace coinbase

#endif  // ! DATA_FEED_COINBASE_L2_DECODER_H_
