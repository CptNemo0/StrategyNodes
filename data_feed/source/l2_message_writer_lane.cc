#include "l2_message_writer_lane.h"

#include <stop_token>
#include <string>

#include "l2_message.h"
#include "spsc_util.h"

namespace data_feed {

L2MessageWriterLane::L2MessageWriterLane(const std::string& symbol)
    : writer_{symbol}, thread_{[this, symbol](const std::stop_token& stop) {
        Drain(
            stop, symbol, queue_,
            [this](const L2Message& message) { writer_.Write(message); },
            [this] { failed_ = true; });
      }} {}

}  // namespace data_feed
