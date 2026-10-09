#ifndef DATA_FEED_L2_FEED_LOOP_H_
#define DATA_FEED_L2_FEED_LOOP_H_

#include <fstream>

#include "l2_kraken_data_feed.h"
#include "l2_message_dispatcher.h"
#include "pipeline_status.h"

namespace data_feed {

// Feed thread body: assumes `feed` is already connected. Blocks on
// `feed.Next()`, logs every raw frame to `output` and hands it to the parser
// via `frames`, and unsubscribes any pair whose writer lane has failed.
// Closes `feed` once `status.stop_requested` is set.
void RunL2FeedLoop(Level2KrakenDataFeed& feed,
                    FrameQueue& frames,
                    const WriterLaneMap& lanes,
                    std::ofstream& output,
                    PipelineStatus& status);

}  // namespace data_feed

#endif  // ! DATA_FEED_L2_FEED_LOOP_H_
