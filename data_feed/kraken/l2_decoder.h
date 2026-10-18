#ifndef DATA_FEED_KRAKEN_L2_DECODER_H_
#define DATA_FEED_KRAKEN_L2_DECODER_H_

#include "net/raw_frame.h"
#include "venue/l2_decoder.h"

namespace kraken {

// Decodes frames of Kraken's v2 "book" channel: one book change per frame,
// for the pair its "data" entry names, with Kraken's checksum and timestamp.
// Other frames (heartbeat, status, acknowledgements) are ignored. A book frame
// missing a field it needs throws, as its layout is no longer the one this
// decoder knows. Keeps nothing across frames.
class L2Decoder final : public venue::L2Decoder {
 public:
  void Decode(const net::RawFrame& frame, Output& output) override;
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_L2_DECODER_H_
