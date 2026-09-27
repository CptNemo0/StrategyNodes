#include "kraken_message_size_util.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "aliasing.h"

namespace data_feed {

namespace {

// Pieces of the "book" snapshot frame - the longest frame the channel sends:
// {"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[
// {"price":84888.7,"qty":0.06980997},...],"asks":[...],
// "checksum":1162203035,"timestamp":"2026-09-27T11:29:39.799080Z"}]}
constexpr std::string_view kSnapshotHead =
    R"({"channel":"book","type":"snapshot","data":[{"symbol":")";
constexpr std::string_view kSnapshotBids = R"(","bids":[)";
constexpr std::string_view kSnapshotAsks = R"(],"asks":[)";
constexpr std::string_view kSnapshotChecksum = R"(],"checksum":)";
constexpr std::string_view kSnapshotTimestamp = R"(,"timestamp":")";
constexpr std::string_view kSnapshotTail = R"("}]})";
constexpr std::string_view kLevelPrice = R"({"price":)";
constexpr std::string_view kLevelQty = R"(,"qty":)";
constexpr std::string_view kLevelTail = "}";

// Longest shortest-round-trip rendering of a double, e.g.
// "-1.2345678901234567e-308". Bounds price and qty for any symbol.
constexpr std::size_t kMaxNumberLength = 24;
// Decimal digits of the largest u32.
constexpr std::size_t kMaxChecksumLength = 10;
// RFC 3339 with microseconds, e.g. "2026-09-27T11:29:39.799080Z".
constexpr std::size_t kTimestampLength = 27;

// One level plus its trailing comma separator.
constexpr std::size_t kMaxLevelLength = kLevelPrice.size() + kMaxNumberLength +
                                        kLevelQty.size() + kMaxNumberLength +
                                        kLevelTail.size() + 1;

// Snapshot length bound for Depth, excluding the symbol.
template <u64 Depth>
  requires(std::ranges::contains(kKrakenL2Depths, Depth))
consteval std::size_t MaxL2SnapshotLength() {
  return kSnapshotHead.size() + kSnapshotBids.size() + kSnapshotAsks.size() +
         2 * Depth * kMaxLevelLength + kSnapshotChecksum.size() +
         kMaxChecksumLength + kSnapshotTimestamp.size() + kTimestampLength +
         kSnapshotTail.size();
}

// MaxL2SnapshotLength<Depth>() for every entry of kKrakenL2Depths, in the same
// order.
constexpr std::array<std::size_t, kKrakenL2Depths.size()>
    kMaxL2SnapshotLengths = []<std::size_t... kIndices>(
                                std::index_sequence<kIndices...>) {
      return std::array{MaxL2SnapshotLength<kKrakenL2Depths[kIndices]>()...};
    }(std::make_index_sequence<kKrakenL2Depths.size()>{});

}  // namespace

std::size_t MaxL2SnapshotLength(u64 depth, std::size_t symbol_length) {
  const auto depth_it = std::ranges::find(kKrakenL2Depths, depth);
  if (depth_it == kKrakenL2Depths.end()) {
    throw std::invalid_argument{
        std::format("Kraken L2 book does not support depth {}", depth)};
  }
  return kMaxL2SnapshotLengths[static_cast<std::size_t>(
             std::distance(kKrakenL2Depths.begin(), depth_it))] +
         symbol_length;
}

}  // namespace data_feed
