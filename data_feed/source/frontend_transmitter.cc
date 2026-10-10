#include "frontend_transmitter.h"

#include <string>
#include <string_view>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"

namespace data_feed {

FrontendTransmitter::FrontendTransmitter(std::string_view /*venue*/,
                                         const std::string& /*symbol*/,
                                         u32 /*depth*/,
                                         const PairPrecision& /*precision*/) {}

void FrontendTransmitter::Receive(L2Message /*message*/) {}

}  // namespace data_feed
