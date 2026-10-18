#ifndef DATA_FEED_VENUE_MANAGER_H_
#define DATA_FEED_VENUE_MANAGER_H_

#include <expected>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "l2/book_spec.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace venue {

// The venues data_feed streams from.
enum class Id : u8 {
  kKraken = 0,
  kCoinbase = 1,
  kVenuesNum,
};

// What a subscription does with its book.
enum class Sink : u8 {
  // Streams it to the frontend (frontend::Transmitter).
  kLive,
  // Records it to a .bin (l2::FileWriter).
  kRecord,
};

// "a live feed" or "a recording", for error details.
constexpr std::string_view SinkName(Sink sink) {
  return sink == Sink::kRecord ? "a recording" : "a live feed";
}

// Identifies a subscription across every venue. Handed out by the Router.
// From the most significant bit: a 48-bit counter, the venue's Id in 8 bits
// and the Sink in the last 8, so ids sort in the order they were handed out
// and name their venue and sink.
using SubscriptionId = u64;

// How many ids the Router can hand out: counters run from 1 to this, the
// largest that fits 48 bits. MakeSubscriptionId() needs a counter in that
// range.
constexpr u64 kMaxSubscriptionsAllowed{(u64{1} << 48) - 1};

constexpr SubscriptionId MakeSubscriptionId(u64 counter, Id venue, Sink sink) {
  return counter << 16 | u64{std::to_underlying(venue)} << 8 |
         std::to_underlying(sink);
}

constexpr u64 CounterOf(SubscriptionId id) {
  return id >> 16;
}

constexpr Id VenueOf(SubscriptionId id) {
  return static_cast<Id>(id >> 8 & 0xFF);
}

constexpr Sink SinkOf(SubscriptionId id) {
  return static_cast<Sink>(id & 0xFF);
}

// One subscription, as listed to the frontend. Its sink is SinkOf(id).
struct SubscriptionInfo {
  SubscriptionId id;
  l2::BookSpec book;
  i64 start_time_ns;
};

// What a venue can stream.
struct PairList {
  // Its pairs, e.g. "BTC/USD", sorted.
  std::vector<std::string> symbols;
  // The book depths it accepts, ascending. Just l2::kFullDepth for a venue
  // that always streams the full book.
  std::vector<u32> depths;
};

// Why a command failed. The codes mirror the wire's CommandError::Code.
struct Error {
  enum class Code : u8 {
    // A venue the Router has no manager for.
    kUnknownVenue,
    // Not a depth the venue accepts.
    kInvalidDepth,
    // The venue does not list the symbol.
    kUnknownSymbol,
    // No subscription has the id.
    kUnknownId,
    // The symbol already has a subscription, as a venue streams a pair at
    // one depth only.
    kSymbolInUse,
    // The venue could not be reached, or answered unexpectedly.
    kVenueFailed,
    // The Router has handed out kMaxSubscriptionsAllowed ids.
    kIdsExhausted,
  };

  Code code;
  // Free text for the UI; never parsed.
  std::string detail;
};

// Thrown by a venue's REST lookups when it does not list a symbol, or lists it
// but it cannot be streamed, so a caller can tell a mistyped symbol from a
// venue that cannot be reached.
class UnknownSymbolError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// An Error to return from a function returning std::expected.
inline std::unexpected<Error> Failure(Error::Code code, std::string detail) {
  return std::unexpected{Error{.code = code, .detail = std::move(detail)}};
}

// Builds a subscription's receiver once the pair's precision is known.
using ReceiverFactoryCallback =
    std::move_only_function<std::unique_ptr<core::Receiver<l2::Message>>(
        const l2::PairPrecision&)>;

// One venue's subscriptions, at most one per symbol, each streaming into a
// receiver the caller builds. Every method may be called from any thread.
class Manager {
 public:
  virtual ~Manager() = default;

  // Recorded in recordings and passed to receivers, e.g. "kraken".
  virtual std::string_view name() const = 0;

  // Connects to the venue and subscribes to every pair. Does nothing while
  // healthy; throws if the venue cannot be reached.
  virtual void Connect() = 0;

  // Whether it is connected and nothing has failed since.
  virtual bool healthy() const = 0;

  // Streams `book` into the receiver `make_receiver` builds, under `id`,
  // which must be unused and name this venue. Returns the id now streaming
  // the pair: `id`, or an existing recording's when SinkOf(id) is kRecord and
  // the pair is already recorded at the same depth, in which case nothing is
  // built. Fails if the symbol already has another subscription.
  virtual std::expected<SubscriptionId, Error> Subscribe(
      SubscriptionId id,
      l2::BookSpec book,
      ReceiverFactoryCallback make_receiver) = 0;

  // Drops the subscription and its receiver, closing a recording. Fails with
  // kUnknownId if no subscription has `id`.
  virtual std::expected<void, Error> Unsubscribe(SubscriptionId id) = 0;

  // Every subscription, ordered by id.
  virtual std::vector<SubscriptionInfo> List() const = 0;

  // The pairs the venue can stream and the depths it accepts. Fails with
  // kVenueFailed if the venue cannot be asked. May block for a round trip
  // to the venue.
  virtual std::expected<PairList, Error> ListPairs() = 0;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_MANAGER_H_
