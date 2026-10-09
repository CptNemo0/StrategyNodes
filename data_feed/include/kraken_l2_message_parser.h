#ifndef DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
#define DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_

#include <optional>
#include <string_view>

#include "aliasing.h"
#include "kraken_pair_precision.h"
#include "l2_message.h"

namespace data_feed {

// Parses a raw Kraken websocket frame. Returns std::nullopt for frames that
// are not from the "book" channel (heartbeat, status etc.) or fail to parse.
// Prices and quantities are scaled to fixed-point integers using `precision`,
// which must have an entry for the message's symbol. `capture_time_ns` is
// stored on the message as-is; it belongs to the caller because only the
// network thread knows when the frame actually arrived.
std::optional<L2Message> ParseKrakenL2Message(
    std::string_view frame,
    i64 capture_time_ns,
    const PairPrecisionMap& precision);

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
