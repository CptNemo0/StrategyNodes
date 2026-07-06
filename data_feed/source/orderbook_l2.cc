#include "orderbook_l2.h"

#include <cstddef>
#include <print>
#include <ranges>
#include <sstream>

#include "aliasing.h"
#include "utility.h"

namespace data_feed {

void OrderbookL2::UpdatePriceLevel(const Level2Record& record,
                                   OrderbookSide side) {
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

u32 OrderbookL2::CalculateChecksum() const {
  constexpr std::ptrdiff_t kChecksumDepth = 10;

  auto append_level = [](std::stringstream& buffer,
                         const Level2Record& level) static {
    buffer << level.price;
    buffer << level.quantity;
  };

  std::stringstream buffer;
  for (const auto& [price, level] :
       sell_side_ | std::views::reverse | std::views::take(kChecksumDepth)) {
    append_level(buffer, level);
  }

  for (const auto& [price, level] :
       buy_side_ | std::views::take(kChecksumDepth)) {
    append_level(buffer, level);
  }

  return Crc32(buffer.str());
}

void OrderbookL2::Log() const {
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

}  // namespace data_feed
