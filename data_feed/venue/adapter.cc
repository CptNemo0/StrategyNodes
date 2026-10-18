#include "venue/adapter.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace venue {

bool IsWellFormedSymbol(std::string_view symbol, std::string_view punctuation) {
  return !symbol.empty() && symbol.size() <= kMaxSymbolLength &&
         std::ranges::all_of(symbol, [punctuation](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                  punctuation.contains(c);
         });
}

}  // namespace venue
