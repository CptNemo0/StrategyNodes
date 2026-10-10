#ifndef DATA_FEED_FRONTEND_TRANSMITTER_H_
#define DATA_FEED_FRONTEND_TRANSMITTER_H_

#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "receiver.h"

namespace data_feed {

// Streams the L2Messages of a single pair to the frontend. Not implemented
// yet: every message is dropped.
class FrontendTransmitter : public Receiver<L2Message> {
 public:
  // Same arguments as L2MessageFileWriter, so either can back a subscription.
  FrontendTransmitter(std::string_view venue,
                      const std::string& symbol,
                      u32 depth,
                      const PairPrecision& precision);

  FrontendTransmitter(const FrontendTransmitter&) = delete;
  FrontendTransmitter& operator=(const FrontendTransmitter&) = delete;

  ~FrontendTransmitter() override = default;

  void Receive(L2Message message) override;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_FRONTEND_TRANSMITTER_H_
