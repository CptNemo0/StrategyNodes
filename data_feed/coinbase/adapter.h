#ifndef DATA_FEED_COINBASE_ADAPTER_H_
#define DATA_FEED_COINBASE_ADAPTER_H_

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coinbase/constants.h"
#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/adapter.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace coinbase {

// Coinbase Advanced Trade's public "level2" channel, for venue::L2Manager. It
// needs no credentials. Coinbase has no depth setting, always streaming a
// product's full book, so the only depth is l2::kFullDepth, and it sends no
// checksum; coinbase::L2Decoder checks the stream's continuity instead.
class Adapter final : public venue::Adapter {
 public:
  std::string_view name() const override { return kVenueName; }

  net::WebsocketFeed::Endpoints endpoints() const override;

  std::span<const u32> depths() const override;

  // Null: Coinbase sends none.
  venue::L2Parser::BookChecksum book_checksum() const override;

  // A product id, upper case as Coinbase spells it, e.g. "BTC-USD".
  std::expected<void, venue::Error> CheckSymbol(
      std::string_view symbol) const override;

  std::expected<l2::PairPrecision, venue::Error> FetchPrecision(
      const std::string& symbol) const override;

  std::expected<std::vector<std::string>, venue::Error> FetchPairs()
      const override;

  // The "heartbeats" subscription, sent as soon as the connection is up:
  // Coinbase drops a connection that subscribes to nothing within 5 seconds,
  // and closes most channels after a minute without updates unless it is
  // subscribed.
  std::vector<std::string> ConnectRequests() const override;

  // On the "level2" channel. The depth is ignored.
  std::string MakeBookRequest(BookRequest request,
                              const l2::BookSpec& book) const override;

  std::unique_ptr<venue::L2Decoder> MakeDecoder() const override;
};

}  // namespace coinbase

#endif  // ! DATA_FEED_COINBASE_ADAPTER_H_
