#include "l2_message_writer_lane.h"

#include <stop_token>
#include <string>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"
#include "spsc_util.h"

namespace data_feed {

L2MessageWriterLane::L2MessageWriterLane(const std::string& symbol,
                                         u32 depth,
                                         const PairPrecision& precision)
    : writer_{symbol, depth, precision},
      thread_{[this, symbol](const std::stop_token& stop) {
        Drain(
            stop, symbol, queue_,
            [this](const L2Message& message) { writer_.Write(message); },
            [this] { failed_ = true; });
      }} {}

}  // namespace data_feed
