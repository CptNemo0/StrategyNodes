#include "level3_parser_threaded.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "orderbook_delta.h"

Level3ParserThreaded::Level3ParserThreaded() {
  // Sort `kFieldNames` array ascending by length, this way when two names
  // start with the same prefix (for example: auction and auction_mode), a
  // first one that appears is the longer one. This eliminates selecting a
  // wrong one by partially matching a prefix.
  std::ranges::sort(kFieldNames,
                    [](const NameFunctionPair& a, const NameFunctionPair& b) {
                      return a.first.size() > b.first.size();
                    });
}

std::unique_ptr<OrderbookDelta> Level3ParserThreaded::Parse(
    std::string_view json) {
  const std::vector<std::string_view> segments =
      SegmentOriginalJson(json, kFieldNames.size());

  if (segments.size() != kFieldNames.size()) {
    return nullptr;
  }

  const u64 estimated_side_size =
      static_cast<u64>(static_cast<float>(json.length()) * 0.5f * 1.1f);

  std::unique_ptr<OrderbookDelta> delta = std::make_unique<OrderbookDelta>();

  delta->asks_.reserve(estimated_side_size);
  delta->bids_.reserve(estimated_side_size);

  {
    std::vector<std::jthread> threads;
    for (auto i{0uz}; i < kFieldNames.size(); ++i) {
      threads.emplace_back(&Level3ParserThreaded::WorkerThread, this,
                           segments[i], delta.get());
    }
  }

  return delta;
}

std::vector<std::string_view> Level3ParserThreaded::SegmentOriginalJson(
    std::string_view json,
    u64 segments_count) {
  constexpr u64 kNone = std::numeric_limits<u64>::max();

  std::vector<std::string_view> segments = std::vector<std::string_view>{};
  segments.reserve(segments_count);

  auto is_space = [](char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
  };

  i64 depth = 0;
  bool in_string = false;
  u64 field_start = kNone;

  // Emits json[start, end) with trailing whitespace trimmed. `start` already
  // points at the field name (just past the key's opening quote), so there is
  // no leading whitespace to trim. Each segment looks like `name":<value>`.
  auto new_field = [&](u64 start, u64 end) {
    segments.emplace_back(json.data() + start, json.data() + end);
    field_start = kNone;
  };

  for (auto i{0uz}; i < json.size(); ++i) {
    const char& current = json[i];

    if (in_string && current == '\"') {
      in_string = false;
      continue;
    }

    switch (current) {
      case '\"':
        in_string = true;
        if (depth == 1 && field_start == kNone) {
          field_start = i + 1;
        }
        break;

      case '}':
        --depth;
        if (depth == 0 && field_start != kNone) {
          new_field(field_start, i);
        }
        break;

      case ',':
        if (depth == 1 && field_start != kNone) {
          new_field(field_start, i);
        }
        break;

      case '{':
      case '[':
        ++depth;
        break;

      case ']':
        --depth;
        break;

      default:
        break;
    }
  }
  return segments;
}

void Level3ParserThreaded::WorkerThread(std::string_view json,
                                        OrderbookDelta* delta) {
  for (const auto& [field_name, function] : kFieldNames) {
    // Current line too short to even consider that `field_name`.
    if (field_name.size() >= json.size()) {
      continue;
    }

    if (field_name ==
        std::string_view{json.data(), json.data() + field_name.size()}) {
      (this->*function)(json, delta);
    }
  }
}

void Level3ParserThreaded::HandleAsks(std::string_view segment,
                                      OrderbookDelta* delta) {
  ParseLadder(segment, delta->asks_);
}

void Level3ParserThreaded::HandleBids(std::string_view segment,
                                      OrderbookDelta* delta) {
  ParseLadder(segment, delta->bids_);
}

void Level3ParserThreaded::HandleSequence(std::string_view segment,
                                          OrderbookDelta* delta) {
  const auto colon = segment.find(':');
  if (colon == std::string_view::npos) {
    return;
  }
  std::from_chars(segment.data() + colon + 1, segment.data() + segment.size(),
                  delta->sequence);
}

void Level3ParserThreaded::ParseLadder(std::string_view segment,
                                       std::vector<Order>& out) {
  // Asks/Bids segments start with '[['.
  u64 i = segment.find('[');

  // If no '[' is found the passed json fragment must be ill-formed.
  if (i == std::string::npos) {
    return;
  }

  // Assign `i` with the position succeeding the position of the first `[`. So
  // that the list opening bracket is beyond the scope of the search in the
  // list. If i + 1 is out of bounds passed json fragment ill-formed.
  ++i;

  while (i < segment.size()) {
    if (i > segment.size()) {
      return;
    }

    // Find the start of the next order
    u64 beginning = segment.find('[', i);

    // Return if no start found. It most likely means that all orders have
    // been parsed. It might mean that there were no orders to be prased.
    if (beginning == std::string_view::npos) {
      return;
    }

    i = beginning;

    const std::optional<std::string_view> price =
        ReadQuotedValue(segment, i, i);
    if (!price.has_value() || i == std::string_view::npos) {
      std::println("Price not found");
      return;
    };
    ++i;

    const std::optional<std::string_view> volume =
        ReadQuotedValue(segment, i, i);
    if (!volume.has_value() || i == std::string_view::npos) {
      std::println("Volume not found");
      return;
    };
    ++i;

    const std::optional<std::string_view> order_id =
        ReadQuotedValue(segment, i, i);
    if (!order_id.has_value() || i == std::string_view::npos) {
      std::println("Order ID not found");
      return;
    };
    ++i;

    Order order{};
    std::from_chars(price->data(), price->data() + price->size(), order.price);
    std::from_chars(volume->data(), volume->data() + volume->size(),
                    order.volume);
    std::copy(order_id->data(), order_id->data() + order_id->size(),
              order.id.begin());
    out.push_back(std::move(order));
  }
}

std::optional<std::string_view> Level3ParserThreaded::ReadQuotedValue(
    std::string_view original_string,
    u64 start_index,
    u64& end_index) {
  if (start_index >= original_string.size()) {
    return std::nullopt;
  }

  u64 i = start_index;

  i = original_string.find('\"', i);
  if (i == std::string_view::npos || i + 1 >= original_string.size()) {
    return std::nullopt;
  }
  // The position succeeding the opening quotation mark's position.
  const u64 start = ++i;

  i = original_string.find('\"', i);
  if (i == std::string_view::npos) {
    return std::nullopt;
  }
  // Closing quotation mark's position.
  const u64 end = i;
  end_index = end;

  return std::string_view{original_string.data() + start,
                          original_string.data() + end};
}
