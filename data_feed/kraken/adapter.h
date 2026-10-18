#ifndef DATA_FEED_KRAKEN_ADAPTER_H_
#define DATA_FEED_KRAKEN_ADAPTER_H_

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/aliasing.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/websocket_token_generator.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/adapter.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace kraken {

// Kraken's v2 "book" channel, for venue::L2Manager. Kraken streams a pair at
// one of kL2Depths and checksums the book's top ten levels with every frame.
// Construction loads the credentials from the environment.
class Adapter final : public venue::Adapter {
 public:
  Adapter();

  std::string_view name() const override { return kVenueName; }

  net::WebsocketFeed::Endpoints endpoints() const override;

  std::span<const u32> depths() const override;

  venue::L2Parser::BookChecksum book_checksum() const override;

  // E.g. "BTC/USD" or "ETH2.S/ETH".
  std::expected<void, venue::Error> CheckSymbol(
      std::string_view symbol) const override;

  // From AssetPairs: pair_decimals and lot_decimals.
  std::expected<l2::PairPrecision, venue::Error> FetchPrecision(
      const std::string& symbol) const override;

  // The AssetPairs whose status is "online".
  std::expected<std::vector<std::string>, venue::Error> FetchPairs()
      const override;

  // None: a book subscription needs nothing before it.
  std::vector<std::string> ConnectRequests() const override { return {}; }

  std::string MakeBookRequest(BookRequest request,
                              const l2::BookSpec& book) const override;

  std::unique_ptr<venue::L2Decoder> MakeDecoder() const override;

 private:
  std::unique_ptr<Credentials> credentials_;
  WebsocketTokenGenerator signer_;
};

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_ADAPTER_H_
