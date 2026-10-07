#include "l2_message_dispatcher.h"

#include <optional>
#include <stop_token>
#include <string>
#include <utility>

#include "kraken_l2_message_parser.h"
#include "l2_message.h"
#include "pipeline_status.h"
#include "spsc_util.h"

namespace data_feed {

void ParseAndDispatch(const std::stop_token& stop,
                      FrameQueue& frames,
                      const WriterLaneMap& lanes,
                      const PairPrecisionMap& precision,
                      PipelineStatus& status) {
  Drain(
      stop, "parser", frames,
      [&lanes, &precision](const std::string& frame) {
        std::optional<L2Message> message =
            ParseKrakenL2Message(frame, precision);
        if (!message) {
          return;
        }
        const auto lane = lanes.find(message->symbol);
        if (lane != lanes.end()) {
          PushBlocking(lane->second->queue(), std::move(*message),
                       lane->second->failed());
        }
      },
      [&status] { status.Fail(); });
}

}  // namespace data_feed
