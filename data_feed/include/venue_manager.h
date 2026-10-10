#ifndef DATA_FEED_VENUE_MANAGER_H_
#define DATA_FEED_VENUE_MANAGER_H_

#include "venues.h"

namespace data_feed {

// Owns everything needed to stream data from one venue: its credentials,
// reference data, data feed and the threads that move frames from the socket
// to the consumers, plus the PipelineStatus those threads share. Only declared
// here; each venue provides its own specialization, since how a venue
// authenticates, connects and frames its messages differs per venue.
template <Venue V>
class VenueManager;

}  // namespace data_feed

#endif  // ! DATA_FEED_VENUE_MANAGER_H_
