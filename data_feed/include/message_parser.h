#ifndef DATA_FEED_MESSAGE_PARSER_H_
#define DATA_FEED_MESSAGE_PARSER_H_

#include "data_level.h"
#include "venues.h"

namespace data_feed {

// Turns a venue's raw frames into messages of one data level and routes each
// to the receiver attached for its symbol; frames for symbols with no
// receiver are ignored. Only declared here; each venue and level provides its
// own specialization, since both decide how a frame is read.
template <Venue V, DataLevel L>
class MessageParser;

}  // namespace data_feed

#endif  // ! DATA_FEED_MESSAGE_PARSER_H_
