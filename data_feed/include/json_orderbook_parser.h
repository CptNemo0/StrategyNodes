#ifndef DATA_FEED_JSON_ORDERBOOK_PARSER_H_
#define DATA_FEED_JSON_ORDERBOOK_PARSER_H_

#include <charconv>
#include <concepts>
#include <optional>
#include <string_view>

#include "aliasing.h"
#include "orderbook_delta.h"
#include "orderbook_entry.h"

class JsonOrderbookParser {
public:
  const OrderbookDelta &delta() const { return delta_; }

  void Parse(std::string_view contents) {

    delta_ = {};  // reset across repeated Parse() calls.
    auto contents_pointer{0uz};
    u64 number_start = 0;
    bool last_was_dot_or_digit = false;
    Mode mode{Mode::kNormal};
    ListMode list_mode{ListMode::kPrice};
    OrderbookEntry orderbook_entry{};
    OrderbookDelta &delta = delta_;
    bool skip_this_entry = false;

    auto check_for_word = [&contents,
                           &contents_pointer](std::string_view word) -> bool {
      if (contents_pointer + word.length() >= contents.length()) {
        return false;
      }
      const std::string_view new_view{contents.data() + contents_pointer,
                                      contents.data() + contents_pointer +
                                          word.length()};
      return new_view == word;
    };

    auto number_extractor = [&]() {
      if (std::isdigit(
              static_cast<unsigned char>(contents[contents_pointer])) ||
          contents[contents_pointer] == '.') {
        if (!last_was_dot_or_digit) {
          number_start = contents_pointer;
          last_was_dot_or_digit = true;
        }
      } else {
        if (last_was_dot_or_digit) {
          if (list_mode == ListMode::kPrice) {
            const std::from_chars_result result = std::from_chars(
                contents.data() + number_start,
                contents.data() + contents_pointer, orderbook_entry.price);
            if (result.ec == std::errc{}) {
              skip_this_entry = true;
            }
            // std::print("price {}\n", orderbook_entry.price);
            list_mode = ListMode::kSize;
          } else if (list_mode == ListMode::kSize) {
            const std::from_chars_result result = std::from_chars(
                contents.data() + number_start,
                contents.data() + contents_pointer, orderbook_entry.size);
            if (result.ec == std::errc{}) {
              skip_this_entry = true;
            }
            // std::print("size {}\n", orderbook_entry.size);
            list_mode = ListMode::kQuantity;
          } else if (list_mode == ListMode::kQuantity) {
            const std::from_chars_result result = std::from_chars(
                contents.data() + number_start,
                contents.data() + contents_pointer, orderbook_entry.quantity);
            // std::print("quantity {}\n", orderbook_entry.quantity);
            if (result.ec == std::errc{}) {
              skip_this_entry = true;
            }
            if (mode == Mode::kAsks) {
              delta.asks_.push_back(orderbook_entry);
            } else {
              delta.bids_.push_back(orderbook_entry);
            }

            list_mode = ListMode::kPrice;
            skip_this_entry = false;
          }

          last_was_dot_or_digit = false;
        }
      }
    };

    auto find_coma = [&]() {
      return Next(contents, contents_pointer, [](char c) { return c == ','; });
    };

    auto find_colon = [&]() {
      return Next(contents, contents_pointer, [](char c) { return c == ':'; });
    };

    auto extract_single_field = [&]() {
      auto next = find_colon();
      if (!next.has_value()) {
        return std::string_view{""};
      }

      const u64 start = contents_pointer + (next.value() + 1);

      next = find_coma();
      if (!next.has_value()) {
        return std::string_view{""};
      }

      const u64 end = contents_pointer + next.value();

      return std::string_view{contents.data() + start, contents.data() + end};
    };

    for (; contents_pointer < contents.size(); ++contents_pointer) {
      switch (contents[contents_pointer]) {
      case 'a':
        if (check_for_word("asks")) {
          contents_pointer += 4;
          mode = Mode::kAsks;
        }
        break;

      case 'b':
        if (check_for_word("bids")) {
          contents_pointer += 4;
          mode = Mode::kBids;
        }
        break;

      case 's': {
        if (check_for_word("sequence")) {
          contents_pointer += 8;
          mode = Mode::kNormal;
          std::string_view result = extract_single_field();
          std::from_chars(result.data(), result.data() + result.length(),
                          delta.sequence);
        }
        break;
      }

      default: {
        if (mode == Mode::kAsks || mode == Mode::kBids) {
          number_extractor();
        } else {
          mode = Mode::kNormal;
        }

        continue;
      }
      }
    }

    // delta.Log();
  }

  std::optional<u64> Next(std::string_view contents, u64 pointer,
                          std::predicate<char> auto predicate) {
    for (u64 internal{pointer}; internal < contents.size(); ++internal) {
      if (predicate(contents[internal])) {
        return internal - pointer;
      }
    }
    return {std::nullopt};
  }

private:
  enum class Mode {
    kAsks,
    kBids,
    kNormal,
  };

  enum class ListMode {
    // In list
    kPrice,
    kSize,
    kQuantity
  };

  OrderbookDelta delta_;
};

#endif // !DATA_FEED_JSON_ORDERBOOK_PARSER_H_
