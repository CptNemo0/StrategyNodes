#include "coinbase/adapter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coinbase/constants.h"
#include "coinbase/l2_decoder.h"
#include "coinbase/products.h"
#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace coinbase {

namespace {

constexpr std::array<u32, 1> kDepths = {l2::kFullDepth};

}  // namespace

net::WebsocketFeed::Endpoints Adapter::endpoints() const {
  return {.host = std::string{kWsHost},
          .port = std::string{kHttpsPort},
          .target = std::string{kWsTarget}};
}

std::span<const u32> Adapter::depths() const {
  return kDepths;
}

venue::L2Parser::BookChecksum Adapter::book_checksum() const {
  return nullptr;
}

std::expected<void, venue::Error> Adapter::CheckSymbol(
    std::string_view symbol) const {
  // Product ids are upper case, and frames name them so: a lower-case id the
  // venue accepted anyway would never match a frame's product_id.
  return venue::IsWellFormedSymbol(symbol, "-") &&
                 std::ranges::none_of(symbol,
                                      [](char c) {
                                        return std::islower(
                                                   static_cast<unsigned char>(
                                                       c)) != 0;
                                      })
             ? std::expected<void, venue::Error>{}
             : venue::Failure(
                   venue::Error::Code::kUnknownSymbol,
                   std::format("A Coinbase product is up to {} capital "
                               "letters, digits and '-', e.g. BTC-USD",
                               venue::kMaxSymbolLength));
}

std::expected<l2::PairPrecision, venue::Error> Adapter::FetchPrecision(
    const std::string& symbol) const {
  return venue::CallVenue([&symbol] { return FetchProductPrecision(symbol); });
}

std::expected<std::vector<std::string>, venue::Error> Adapter::FetchPairs()
    const {
  return venue::CallVenue(FetchTradableProducts);
}

std::vector<std::string> Adapter::ConnectRequests() const {
  return {R"({"type":"subscribe","channel":"heartbeats"})"};
}

std::string Adapter::MakeBookRequest(BookRequest request,
                                     const l2::BookSpec& book) const {
  return std::format(
      R"({{"type":"{}","channel":"level2","product_ids":["{}"]}})",
      request == BookRequest::kSubscribe ? "subscribe" : "unsubscribe",
      book.symbol);
}

std::unique_ptr<venue::L2Decoder> Adapter::MakeDecoder() const {
  return std::make_unique<L2Decoder>();
}

}  // namespace coinbase
