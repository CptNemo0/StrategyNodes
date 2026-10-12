#ifndef DATA_FEED_VENUE_MANAGER_H_
#define DATA_FEED_VENUE_MANAGER_H_

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace venue {

// The venues data_feed streams from.
enum class Id : u8 { kKraken };

// Identifies a subscription across every venue. Handed out by the Router.
using SubscriptionId = u64;

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

// One subscription, as listed to the frontend.
struct SubscriptionInfo {
  SubscriptionId id;
  // E.g. "BTC/USD".
  std::string symbol;
  u32 depth;
  Sink sink;
  // When it was made, nanoseconds since the Unix epoch.
  i64 start_time_ns;
};

// What a venue can stream.
struct PairList {
  // Its pairs, e.g. "BTC/USD", sorted.
  std::vector<std::string> symbols;
  // The book depths it accepts, ascending.
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
    // The subscription exists, but with another sink than the command named.
    kSinkMismatch,
    // The symbol already has a subscription, as a venue streams a pair at
    // one depth only.
    kSymbolInUse,
    // The venue could not be reached, or answered unexpectedly.
    kVenueFailed,
  };

  Code code;
  // Free text for the UI; never parsed.
  std::string detail;
};

// Builds a subscription's receiver once the pair's precision is known.
using ReceiverFactory =
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

  // Streams `symbol`'s book at `depth` into the receiver `make_receiver`
  // builds, under `id`, which must be unused. Returns the id now streaming
  // the pair: `id`, or an existing kRecord subscription's when `sink` is
  // kRecord and the pair is already recorded at `depth`, in which case
  // nothing is built. Fails if the symbol already has another subscription.
  virtual std::expected<SubscriptionId, Error> Subscribe(
      SubscriptionId id,
      const std::string& symbol,
      u32 depth,
      Sink sink,
      ReceiverFactory make_receiver) = 0;

  // Drops the subscription and its receiver, closing a recording. Fails with
  // kUnknownId if no subscription has `id`, or kSinkMismatch if it has
  // another sink than `sink`, so that a command meant for a recording cannot
  // stop a live feed. Checked under the same lock Subscribe() resolves an
  // existing recording under, so the two cannot interleave.
  virtual std::expected<void, Error> Unsubscribe(SubscriptionId id,
                                                 Sink sink) = 0;

  // Every subscription, ordered by id.
  virtual std::vector<SubscriptionInfo> List() const = 0;

  // The pairs the venue can stream and the depths it accepts. Fails with
  // kVenueFailed if the venue cannot be asked. May block for a round trip
  // to the venue.
  virtual std::expected<PairList, Error> ListPairs() = 0;
};

}  // namespace venue

#endif  // ! DATA_FEED_VENUE_MANAGER_H_
