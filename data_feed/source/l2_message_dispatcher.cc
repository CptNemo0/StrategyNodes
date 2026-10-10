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
                      const FileWriterMap& writers,
                      const PairPrecisionMap& precision,
                      PipelineStatus& status) {
  Drain(
      stop, "parser", frames,
      [&writers, &precision](const RawFrame& frame) {
        std::optional<L2Message> message =
            ParseKrakenL2Message(frame.json, frame.capture_time_ns, precision);
        if (!message) {
          return;
        }
        const auto writer = writers.find(message->symbol);
        if (writer != writers.end()) {
          PushBlocking(writer->second->queue(), std::move(*message),
                       writer->second->failed());
        }
      },
      [&status] { status.Fail(); });
}

}  // namespace data_feed
