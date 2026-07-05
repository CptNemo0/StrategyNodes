#ifndef DATA_FEED_ORDERBOOK_L2_H_
#define DATA_FEED_ORDERBOOK_L2_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <flat_map>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aliasing.h"

namespace data_feed {

enum class OrderbookSide { kBuy = 0, kSell = 1 };

struct Level2Record {
  i64 price{};
  i64 quantity{};
  std::string to_string() const {
    return std::format("{:>13.8f} @ {:>7.1f}", quantity / 100000000.0,
                       price / 10.0);
  }
};

// template <typename ComparsionOperator>
// struct Level2RecordPriceComparer {
//   static bool operator()(const Level2Record& lhs, const Level2Record& rhs) {
//     return ComparsionOperator{}(lhs.price < rhs.price);
//   }
// };

class OrderbookL2 {
 public:
  using Side = std::flat_map<i64, Level2Record, std::greater<>>;

  explicit OrderbookL2(u64 depth) : depth_(depth) {}

  i64 BidPrice() const {
    return buy_side_.size() > 0 ? buy_side_.begin()->second.price : 0;
  }

  i64 AskPrice() const {
    return buy_side_.size() > 0 ? buy_side_.rbegin()->second.price
                                : std::numeric_limits<i64>::max();
  }

  void AddRecord(const Level2Record& record, OrderbookSide side) {
    Side& records = GetSideRecords(side);
    auto iterator = records.find(record.price);
    const bool price_level_exists = iterator != records.end();

    // A quantity equal to zero signals that a corresponding
    // price level should be erased.
    if (record.quantity == 0 && price_level_exists) {
      records.erase(iterator);
      return;
    }

    if (price_level_exists) {
      // The specified price level exists, thus update the quantity at that
      // level.
      iterator->second.quantity = record.quantity;
      return;
    }

    // The specified price level does not exist, thus it needs to be inserted.
    records.insert({record.price, record});

    // If the number of price levels does not exceed the max depth of the
    // orderbook return to the caller, otherwise remove the worst price level
    // from the orderbook.
    if (records.size() <= depth_) {
      return;
    }

    if (side == OrderbookSide::kBuy) {
      records.erase(records.rbegin()->second.price);
    } else {
      records.erase(records.begin());
    }
  };

  // https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2
  u32 CalculateChecksum() const {
    constexpr std::ptrdiff_t kChecksumDepth = 10;

    auto append_level = [](std::string& buffer, const Level2Record& level) {
      buffer += std::to_string(level.price);
      buffer += std::to_string(level.quantity);
    };

    std::string buffer;
    for (const auto& [price, level] :
         sell_side_ | std::views::reverse | std::views::take(kChecksumDepth)) {
      append_level(buffer, level);
    }
    for (const auto& [price, level] :
         buy_side_ | std::views::take(kChecksumDepth)) {
      append_level(buffer, level);
    }

    return Crc32(buffer);
  }

  void Log() const {
    std::stringstream ss;
    ss << "========= BUY SIDE =========\n";
    for (const auto& [price, level] : buy_side_) {
      ss << level.to_string() << "\n";
    }

    ss << "========= SELL SIDE ========\n";
    for (const auto& [price, level] : sell_side_) {
      ss << level.to_string() << "\n";
    }

    std::println("{}", ss.str());
  }

 private:
  static constexpr std::array<u32, 256> BuildCrc32Table() {
    std::array<u32, 256> table{};
    for (u32 value{}; value < table.size(); ++value) {
      u32 crc = value;
      for (u32 bit{}; bit < 8; ++bit) {
        crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
      }
      table[value] = crc;
    }
    return table;
  }

  static u32 Crc32(std::string_view data) {
    static constexpr std::array<u32, 256> kTable = BuildCrc32Table();

    u32 crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) {
      crc = kTable[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
  }

  Side& GetSideRecords(OrderbookSide side) {
    if (side == OrderbookSide::kBuy) {
      return buy_side_;
    } else {
      return sell_side_;
    }
  }

  u64 depth_{std::numeric_limits<u64>::max()};
  Side buy_side_;
  Side sell_side_;
};

}  // namespace data_feed

#endif  // ! DATA_FEED_ORDERBOOK_L2_H_
