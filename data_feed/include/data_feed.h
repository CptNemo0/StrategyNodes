#ifndef DATA_FEED_DATA_FEED_H_
#define DATA_FEED_DATA_FEED_H_

#include "protocol.h"

namespace data_feed {

// A connection to a venue that hands out raw frames, stamped with their
// capture time. Knows nothing about the venue or what the frames mean -- the
// venue's VenueManager tells it what to send, and its MessageParser makes
// sense of what comes back. Only declared here; each protocol provides its
// own specialization.
template <Protocol P>
class DataFeed;

}  // namespace data_feed

#endif  // ! DATA_FEED_DATA_FEED_H_
