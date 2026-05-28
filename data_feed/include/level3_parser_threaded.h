#ifndef DATA_FEED_LEVEL_3_PARSER_THREDED_H_
#define DATA_FEED_LEVEL_3_PARSER_THREDED_H_

#include <optional>
#include <string_view>
#include <vector>

#include "aliasing.h"
#include "orderbook_delta.h"

class Level3ParserThreaded {
 public:
  void Parse(std::string_view json);

  const OrderbookDelta& delta() { return delta_; }

 private:
  using FieldHandler = void (Level3ParserThreaded::*)(std::string_view);
  using NameFunctionPair = std::pair<std::string_view, FieldHandler>;

  // Segments the root JSON object into one string_view per top-level field,
  // each of the form `name":<value>` (name without its opening quote, value
  // verbatim). Tracks string state and brace/bracket depth, so it is robust to
  // whitespace/pretty-printing and to commas nested inside arrays or strings.
  // `segments_count` is only a reserve hint.
  void SegmentOriginalJson(std::string_view json, u64 segments_count);

  void WorkerThread(std::string_view json);

  void HandleAsks(std::string_view segment);

  void HandleBids(std::string_view segment);

  void HandleSequence(std::string_view segment);

  void HandleAuctionMode([[maybe_unused]] std::string_view) {}
  void HandleAuction([[maybe_unused]] std::string_view) {}
  void HandleTime([[maybe_unused]] std::string_view) {}

  void ParseLadder(std::string_view segment, std::vector<Order>& out);

  // Reads the first value enclosed in quotation marks - "" - that appears after
  // `start_index`. `start_index` can be a position of an opening quotation
  // mark. On success, returns string_view of the value (without quotation
  // marks) AND assigns `end_index` with the position of the closing bracket in
  // the `original_string`. On failure returns std::nullopt AND does not modify
  // `end_index`.
  std::optional<std::string_view> ReadQuotedValue(
      std::string_view original_string,
      u64 start_index,
      u64& end_index);

  std::array<NameFunctionPair, 6> kFieldNames{{
      {"auction_mode", &Level3ParserThreaded::HandleAuctionMode},
      {"sequence", &Level3ParserThreaded::HandleSequence},
      {"auction", &Level3ParserThreaded::HandleAuction},
      {"asks", &Level3ParserThreaded::HandleAsks},
      {"bids", &Level3ParserThreaded::HandleBids},
      {"time", &Level3ParserThreaded::HandleTime},
  }};

  std::vector<std::string_view> segments_;
  OrderbookDelta delta_;
};

#endif  // !DATA_FEED_LEVEL_3_PARSER_THREDED_H_
