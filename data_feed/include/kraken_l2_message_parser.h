#ifndef DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
#define DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_

#include <optional>
#include <string_view>

#include "l2_message.h"

namespace data_feed {

// Parses a raw Kraken websocket frame. Returns std::nullopt for frames that
// are not from the "book" channel (heartbeat, status etc.) or fail to parse.
// Prices and quantities are packed into i64 by double_string_to_i64.
std::optional<L2Message> ParseKrakenL2Message(std::string_view frame);

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_L2_MESSAGE_PARSER_H_
