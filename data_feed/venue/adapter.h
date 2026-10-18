#ifndef DATA_FEED_VENUE_ADAPTER_H_
#define DATA_FEED_VENUE_ADAPTER_H_

#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/aliasing.h"
#include "l2/book_spec.h"
#include "l2/pair_precision.h"
#include "net/websocket_feed.h"
#include "venue/l2_decoder.h"
#include "venue/l2_parser.h"
#include "venue/manager.h"

namespace venue {

// The longest symbol IsWellFormedSymbol() accepts. Venues' run to a dozen
// characters or so; anything far longer is not one.
inline constexpr std::size_t kMaxSymbolLength = 32;

// Whether `symbol` is 1 to kMaxSymbolLength letters, digits and characters of
// `punctuation`, so it can go into a request as it is.
bool IsWellFormedSymbol(std::string_view symbol, std::string_view punctuation);

// What `fetch`, a call to the venue, returns, or kUnknownSymbol if it throws
// UnknownSymbolError and kVenueFailed if it throws anything else.
template <typename Fetch>
std::expected<std::invoke_result_t<Fetch&>, Error> CallVenue(Fetch fetch) {
  try {
    return fetch();
  } catch (const UnknownSymbolError& e) {
    return Failure(Error::Code::kUnknownSymbol, e.what());
  } catch (const std::exception& e) {
    return Failure(Error::Code::kVenueFailed, e.what());
  }
}

// Everything venue::L2Manager needs to know about one venue: where its feed
// is, what it accepts, how its requests are spelled and how its frames are
// decoded. The manager does the rest, the same for every venue.
//
// Every method may be called from any thread.
class Adapter {
 public:
  enum class BookRequest : u8 { kSubscribe, kUnsubscribe };

  virtual ~Adapter() = default;

  // Recorded in recordings and passed to receivers, e.g. "kraken".
  virtual std::string_view name() const = 0;

  // Where the feed connects.
  virtual net::WebsocketFeed::Endpoints endpoints() const = 0;

  // The book depths it accepts, ascending. Just l2::kFullDepth for a venue
  // that always streams the full book.
  virtual std::span<const u32> depths() const = 0;

  // Its checksum over a book, or null if it sends none.
  virtual L2Parser::BookChecksum book_checksum() const = 0;

  // Fails with kUnknownSymbol if `symbol` cannot name one of its pairs.
  // Checked before the symbol goes into any request, so one from a client
  // cannot add anything to it.
  virtual std::expected<void, Error> CheckSymbol(
      std::string_view symbol) const = 0;

  // The pair's precision. Blocks for a round trip to the venue. Fails with
  // kUnknownSymbol if the venue does not list it, or kVenueFailed.
  virtual std::expected<l2::PairPrecision, Error> FetchPrecision(
      const std::string& symbol) const = 0;

  // Its tradable pairs, sorted. Blocks for a round trip to the venue. Fails
  // with kVenueFailed.
  virtual std::expected<std::vector<std::string>, Error> FetchPairs() const = 0;

  // The frames every new connection sends first, before any subscription.
  virtual std::vector<std::string> ConnectRequests() const = 0;

  // The frame that subscribes to or unsubscribes from `book`.
  virtual std::string MakeBookRequest(BookRequest request,
                                      const l2::BookSpec& book) const = 0;

  // A decoder for a new connection.
  virtual std::unique_ptr<L2Decoder> MakeDecoder() const = 0;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_ADAPTER_H_
