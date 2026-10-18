#include "kraken/adapter.h"

#include <array>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/aliasing.h"
#include "kraken/checksum.h"
#include "kraken/constants.h"
#include "kraken/credentials.h"
#include "kraken/l2_decoder.h"
#include "kraken/message_size.h"
#include "kraken/pair_precision.h"
#include "kraken/tradable_pairs.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace kraken {

Adapter::Adapter()
    : credentials_{Credentials::FromEnvironment()}, signer_{*credentials_} {}

net::WebsocketFeed::Endpoints Adapter::endpoints() const {
  return {.host = std::string{kWsL2Host},
          .port = std::string{kHttpsPort},
          .target = std::string{kWsL2Target}};
}

std::span<const u32> Adapter::depths() const {
  return kL2Depths;
}

venue::L2Parser::BookChecksum Adapter::book_checksum() const {
  return &BookChecksum;
}

std::expected<void, venue::Error> Adapter::CheckSymbol(
    std::string_view symbol) const {
  return venue::IsWellFormedSymbol(symbol, "/.-_")
             ? std::expected<void, venue::Error>{}
             : venue::Failure(
                   venue::Error::Code::kUnknownSymbol,
                   std::format("A Kraken pair is up to {} letters, "
                               "digits and '/', '.', '-' or '_', e.g. BTC/USD",
                               venue::kMaxSymbolLength));
}

std::expected<l2::PairPrecision, venue::Error> Adapter::FetchPrecision(
    const std::string& symbol) const {
  return venue::CallVenue(
      [&symbol] { return FetchPairPrecision(std::array{symbol}).at(symbol); });
}

std::expected<std::vector<std::string>, venue::Error> Adapter::FetchPairs()
    const {
  return venue::CallVenue(FetchTradablePairs);
}

std::string Adapter::MakeBookRequest(BookRequest request,
                                     const l2::BookSpec& book) const {
  return std::format(
      R"({{"method":"{}","params":{{"channel":"book","depth":{},"symbol":["{}"]}}}})",
      request == BookRequest::kSubscribe ? "subscribe" : "unsubscribe",
      book.depth, book.symbol);
}

std::unique_ptr<venue::L2Decoder> Adapter::MakeDecoder() const {
  return std::make_unique<L2Decoder>();
}

}  // namespace kraken
