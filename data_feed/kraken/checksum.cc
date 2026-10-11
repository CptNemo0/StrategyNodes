#include "kraken/checksum.h"

#include <format>
#include <iterator>
#include <ranges>
#include <string>

#include "core/aliasing.h"
#include "core/crc32.h"
#include "l2/book.h"

namespace kraken {

namespace {

// Appends the top kChecksumDepth levels of `side` to `out`.
template <typename Side>
void AppendSide(const Side& side, std::string& out) {
  for (const auto& [price, quantity] :
       side | std::views::take(kChecksumDepth)) {
    std::format_to(std::back_inserter(out), "{}{}", price, quantity);
  }
}

}  // namespace

u32 BookChecksum(const l2::Book& book) {
  std::string text;
  AppendSide(book.asks(), text);
  AppendSide(book.bids(), text);
  return core::Crc32(text);
}

}  // namespace kraken
