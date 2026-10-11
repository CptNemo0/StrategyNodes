#include "frontend/wire.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message_lite.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/aliasing.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "market_data.pb.h"
#include "venue/manager.h"

namespace frontend {

namespace {

// The protobuf wire types Frame() writes.
constexpr u32 kWireTypeVarint = 0;
constexpr u32 kWireTypeFixed64 = 1;
constexpr u32 kWireTypeLengthDelimited = 2;

constexpr u32 Tag(int field_number, u32 wire_type) {
  return (static_cast<u32>(field_number) << 3) | wire_type;
}

Payload Serialize(int field_number,
                  const google::protobuf::MessageLite& message) {
  return Payload{.field_number = field_number,
                 .bytes = std::make_shared<const std::string>(
                     message.SerializeAsString())};
}

wire::CommandError::Code ToWire(venue::Error::Code code) {
  switch (code) {
    case venue::Error::Code::kUnknownVenue:
      // The venue enum on the wire names only venues the backend knows, so
      // an unknown one is a malformed command.
      return wire::CommandError::CODE_INVALID_COMMAND;
    case venue::Error::Code::kInvalidDepth:
      return wire::CommandError::CODE_INVALID_DEPTH;
    case venue::Error::Code::kUnknownSymbol:
      return wire::CommandError::CODE_UNKNOWN_SYMBOL;
    case venue::Error::Code::kUnknownId:
      return wire::CommandError::CODE_UNKNOWN_ID;
    case venue::Error::Code::kSinkMismatch:
      return wire::CommandError::CODE_SINK_MISMATCH;
    case venue::Error::Code::kSymbolInUse:
      return wire::CommandError::CODE_SYMBOL_IN_USE;
    case venue::Error::Code::kVenueFailed:
      return wire::CommandError::CODE_VENUE_FAILED;
  }
  return wire::CommandError::CODE_UNSPECIFIED;
}

}  // namespace

wire::StreamInfo MakeStreamInfo(venue::SubscriptionId id,
                                std::string_view venue,
                                std::string_view symbol,
                                u32 depth,
                                const l2::PairPrecision& precision) {
  wire::StreamInfo info;
  info.set_stream_id(id);
  info.set_symbol(std::string{symbol});
  info.set_venue(std::string{venue});
  info.set_depth(depth);
  info.set_price_decimals(precision.price_decimals);
  info.set_qty_decimals(precision.qty_decimals);
  return info;
}

wire::Book MakeBook(venue::SubscriptionId id, const l2::Message& message) {
  wire::Book book;
  book.set_stream_id(id);
  book.set_kind(message.type == l2::Message::Type::kSnapshot
                    ? wire::Book::KIND_SNAPSHOT
                    : wire::Book::KIND_DELTA);
  const auto price_and_quantity = [](const l2::Message::Level& level) {
    return std::pair{level.price, level.quantity};
  };
  AppendLevels(message.buys, price_and_quantity, *book.mutable_bids());
  AppendLevels(message.sells, price_and_quantity, *book.mutable_asks());
  book.set_checksum(message.checksum);
  book.set_venue_time_ns(message.venue_time_ns);
  book.set_capture_time_ns(message.capture_time_ns);
  return book;
}

Payload MakePayload(const wire::StreamInfo& info) {
  return Serialize(wire::MarketDataEvent::kStreamInfoFieldNumber, info);
}

Payload MakePayload(const wire::Book& book) {
  return Serialize(wire::MarketDataEvent::kBookFieldNumber, book);
}

Payload MakePayload(const wire::FeedStatus& status) {
  return Serialize(wire::MarketDataEvent::kFeedStatusFieldNumber, status);
}

Payload MakePayload(const wire::Heartbeat& heartbeat) {
  return Serialize(wire::MarketDataEvent::kHeartbeatFieldNumber, heartbeat);
}

Payload MakePayload(const wire::CommandResult& result) {
  return Serialize(wire::MarketDataEvent::kResultFieldNumber, result);
}

std::string Frame(u64 sequence, i64 send_time_ns, const Payload& payload) {
  std::string frame;
  {
    google::protobuf::io::StringOutputStream raw{&frame};
    google::protobuf::io::CodedOutputStream out{&raw};
    out.WriteTag(
        Tag(wire::MarketDataEvent::kSequenceFieldNumber, kWireTypeVarint));
    out.WriteVarint64(sequence);
    out.WriteTag(
        Tag(wire::MarketDataEvent::kSendTimeNsFieldNumber, kWireTypeFixed64));
    out.WriteLittleEndian64(static_cast<u64>(send_time_ns));
    out.WriteTag(Tag(payload.field_number, kWireTypeLengthDelimited));
    out.WriteVarint32(static_cast<u32>(payload.bytes->size()));
    out.WriteString(*payload.bytes);
  }  // Destroying the streams trims `frame` to what was written.
  return frame;
}

wire::CommandError MakeCommandError(const venue::Error& error) {
  wire::CommandError wire_error;
  wire_error.set_code(ToWire(error.code));
  wire_error.set_detail(error.detail);
  return wire_error;
}

wire::Sink ToWire(venue::Sink sink) {
  return sink == venue::Sink::kRecord ? wire::SINK_RECORD : wire::SINK_LIVE;
}

wire::Venue ToWire(venue::Id venue) {
  switch (venue) {
    case venue::Id::kKraken:
      return wire::VENUE_KRAKEN;
  }
  return wire::VENUE_UNSPECIFIED;
}

std::optional<venue::Sink> FromWire(wire::Sink sink) {
  switch (sink) {
    case wire::SINK_LIVE:
      return venue::Sink::kLive;
    case wire::SINK_RECORD:
      return venue::Sink::kRecord;
    default:
      return std::nullopt;
  }
}

std::optional<venue::Id> FromWire(wire::Venue venue) {
  return venue == wire::VENUE_KRAKEN ? std::optional{venue::Id::kKraken}
                                     : std::nullopt;
}

}  // namespace frontend
