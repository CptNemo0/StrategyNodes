#ifndef DATA_FEED_VENUE_L2_DECODER_H_
#define DATA_FEED_VENUE_L2_DECODER_H_

#include <string_view>

#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"

namespace venue {

// Turns one venue's websocket frames into l2::Messages: the only part of
// parsing a book stream that depends on the venue's message layout.
// venue::L2Parser does the rest (routing, snapshot gating, checksums).
//
// One per connection: Adapter::MakeDecoder() builds a fresh one for every
// Connect(), so whatever it tracks across frames (e.g. a sequence number)
// starts over with the connection. Used on the parser thread only.
class L2Decoder {
 public:
  // Where Decode() hands what it finds. Implemented by venue::L2Parser.
  class Output {
   public:
    // The precision to scale a book change of `symbol` at, or null if the
    // change would be dropped anyway (no receiver for the pair, or an update
    // before its snapshot), in which case its levels need not be parsed.
    virtual const l2::PairPrecision* Find(std::string_view symbol,
                                          l2::Message::Type type) const = 0;

    // A book change Find() gave a precision for.
    virtual void Emit(l2::Message message) = 0;

   protected:
    ~Output() = default;
  };

  virtual ~L2Decoder() = default;

  // Parses `frame` and emits each book change in it, in order. Frames that
  // carry none (heartbeats, acknowledgements) emit nothing. Throws if a book
  // frame is not laid out as the venue documents, or if the stream shows a
  // gap, so the pipeline fails and the connection is rebuilt with fresh
  // snapshots.
  virtual void Decode(const net::RawFrame& frame, Output& output) = 0;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_L2_DECODER_H_
