#ifndef DATA_FEED_FRONTEND_WIRE_H_
#define DATA_FEED_FRONTEND_WIRE_H_

#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "market_data.pb.h"
#include "venue/manager.h"

// Conversions between data_feed's own types and the generated wire types of
// proto/market_data.proto. The generated types go no further than frontend/.
namespace frontend {

namespace wire = ::trading::market_data::v1;

// Describes the stream `id` carries, so a client can decode its books.
wire::StreamInfo MakeStreamInfo(venue::SubscriptionId id,
                                std::string_view venue,
                                std::string_view symbol,
                                u32 depth,
                                const l2::PairPrecision& precision);

// `message` as a Book of stream `id`, its levels in the order the venue sent
// them.
wire::Book MakeBook(venue::SubscriptionId id, const l2::Message& message);

// Copies `levels` onto the end of `out`, in order; `price_and_quantity` gives
// each one's {price, quantity}.
template <std::ranges::sized_range Levels, typename PriceAndQuantity>
void AppendLevels(const Levels& levels,
                  PriceAndQuantity price_and_quantity,
                  google::protobuf::RepeatedPtrField<wire::Level>& out) {
  out.Reserve(out.size() + static_cast<int>(std::ranges::size(levels)));
  for (const auto& level : levels) {
    const auto [price, quantity] = price_and_quantity(level);
    wire::Level& added = *out.Add();
    added.set_price(price);
    added.set_quantity(quantity);
  }
}

// One MarketDataEvent payload, serialized once and shared by every client it
// goes to. Each client wraps it in an envelope of its own, since the sequence
// and send time differ per client; see Frame().
struct Payload {
  // Its field number in MarketDataEvent, e.g.
  // wire::MarketDataEvent::kBookFieldNumber.
  int field_number;
  std::shared_ptr<const std::string> bytes;
};

Payload MakePayload(const wire::StreamInfo& info);
Payload MakePayload(const wire::Book& book);
Payload MakePayload(const wire::FeedStatus& status);
Payload MakePayload(const wire::Heartbeat& heartbeat);
Payload MakePayload(const wire::CommandResult& result);

// The serialized MarketDataEvent carrying `payload`, with `sequence` and
// `send_time_ns`. A protobuf message is the concatenation of its fields, so
// the payload's bytes are copied in as they are rather than encoded again.
std::string Frame(u64 sequence, i64 send_time_ns, const Payload& payload);

// The CommandError a venue::Error is sent as.
wire::CommandError MakeCommandError(const venue::Error& error);

wire::Sink ToWire(venue::Sink sink);

wire::Venue ToWire(venue::Id venue);

// The sink a command names; nothing for SINK_UNSPECIFIED or an unknown value.
std::optional<venue::Sink> FromWire(wire::Sink sink);

// The venue a command names; nothing for VENUE_UNSPECIFIED or an unknown
// value.
std::optional<venue::Id> FromWire(wire::Venue venue);

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_WIRE_H_
