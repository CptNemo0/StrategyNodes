#include <algorithm>
#include <array>
#include <barrier>
#include <boost/beast/http/detail/rfc7230.hpp>
#include <cctype>
#include <charconv>
#include <chrono>
#include <execution>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aliasing.h"
#include "json_orderbook_parser.h"
#include "orderbook_delta.h"
#include "orderbook_entry.h"

const std::filesystem::path test_path{
    R"(C:\Users\pawel\Desktop\Programowanie\trading_system\example_responses\orderbook\level1.txt)"};

class ThreadedJsonOrderbookParser {
public:
  void Parse(std::string_view json) {
    delta_ = {}; // reset across repeated Parse() calls.

    // Sort `kFieldNames` array ascending by length, this way when two names
    // start with the same prefix (for example: auction and auction_mode), a
    // first one that appears is the longer one. This eliminates selecting a
    // wrong one by partially matching a prefix.
    std::ranges::sort(kFieldNames,
                      [](const NameFunctionPair &a, const NameFunctionPair &b) {
                        return a.first.size() > b.first.size();
                      });

    Segment(json, kFieldNames.size());

    std::vector<std::jthread> threads;
    for (auto i{0uz}; i < kFieldNames.size(); ++i) {
      threads.emplace_back(&ThreadedJsonOrderbookParser::WorkerThread, this,
                           segments_[i]);
    }
  };

  const OrderbookDelta &delta() { return delta_; }

private:
  using FieldHandler = void (ThreadedJsonOrderbookParser::*)(std::string_view);
  using NameFunctionPair = std::pair<std::string_view, FieldHandler>;

  // Segments the json into `segments_count` separate spans. This function is
  // based on an assumption that each name of the field starts with an
  // alphabetic letter.
  void Segment(std::string_view json, u64 segments_count) {
    constexpr std::array<std::string_view, 2> beginnings{"{\"", ",\""};
    auto is_beginning = [&](u64 i) {
      return (json[i] == beginnings[0][0] && json[i + 1] == beginnings[0][1] &&
              std::isalpha(static_cast<unsigned char>(json[i + 2]))) ||
             (json[i] == beginnings[1][0] && json[i + 1] == beginnings[1][1] &&
              std::isalpha(static_cast<unsigned char>(json[i + 2])));
    };

    segments_ = std::vector<std::string_view>{};
    segments_.reserve(segments_count);

    u64 beginning = std::numeric_limits<u64>::max();

    for (auto i{0uz}; i < json.size() - 2; ++i) {
      if (is_beginning(i)) {
        if (beginning == std::numeric_limits<u64>::max()) {
          beginning = i;
        } else {
          // You add 2 to eliminate {" and ," form the beginning.
          segments_.emplace_back(json.data() + beginning + 2, json.data() + i);
          beginning = i;
        }
      }
    }

    segments_.emplace_back(json.data() + beginning + 2,
                           json.data() + json.size() - 1);
  }

  void WorkerThread(std::string_view json) {

    for (const auto &[field_name, function] : kFieldNames) {
      // Current line too short to even consider that `field_name`.
      if (field_name.size() >= json.size()) {
        continue;
      }

      if (field_name ==
          std::string_view{json.data(), json.data() + field_name.size()}) {
        (this->*function)(json);
      }
    }
  }

  void HandleAsks(std::string_view segment) {
    ParseLadder(segment, delta_.asks_);
  }

  void HandleBids(std::string_view segment) {
    ParseLadder(segment, delta_.bids_);
  }

  void HandleSequence(std::string_view segment) {
    const auto colon = segment.find(':');
    if (colon == std::string_view::npos)
      return;
    std::from_chars(segment.data() + colon + 1, segment.data() + segment.size(),
                    delta_.sequence);
  }

  // OrderbookDelta has no fields for these yet — leave as no-ops; extend the
  // struct (and these handlers) when the downstream code needs them.
  void HandleAuctionMode(std::string_view /*segment*/) {}
  void HandleAuction(std::string_view /*segment*/) {}
  void HandleTime(std::string_view /*segment*/) {}

  // Parses the `[["<price>","<size>",<qty>], ...]` array sitting inside the
  // segment and appends one `OrderbookEntry` per triplet.
  static void ParseLadder(std::string_view segment,
                          std::vector<OrderbookEntry> &out) {
    u64 i = segment.find('[');
    if (i == std::string_view::npos)
      return;
    // past outer '['
    ++i;

    while (i < segment.size() && segment[i] != ']') {
      if (segment[i] != '[') {
        ++i;
        continue;
      }
      // past inner '['
      ++i;

      OrderbookEntry entry{};

      // Price and size are quoted strings: skip to '"', read until next '"'.
      auto read_quoted_double = [&](f64 &dst) {
        while (i < segment.size() && segment[i] != '"')
          ++i;
        ++i;
        const u64 start = i;
        while (i < segment.size() && segment[i] != '"')
          ++i;
        std::from_chars(segment.data() + start, segment.data() + i, dst);
        // past closing '"'
        ++i;
      };
      read_quoted_double(entry.price);
      read_quoted_double(entry.size);

      // Quantity is a bare integer: skip to ',', read until ']'.
      while (i < segment.size() && segment[i] != ',')
        ++i;
      ++i;
      const u64 qty_start = i;
      while (i < segment.size() && segment[i] != ']')
        ++i;
      std::from_chars(segment.data() + qty_start, segment.data() + i,
                      entry.quantity);
      // past inner ']'
      ++i;

      out.push_back(entry);
    }
  }

  std::array<NameFunctionPair, 6> kFieldNames{{
      {"auction_mode", &ThreadedJsonOrderbookParser::HandleAuctionMode},
      {"sequence", &ThreadedJsonOrderbookParser::HandleSequence},
      {"auction", &ThreadedJsonOrderbookParser::HandleAuction},
      {"asks", &ThreadedJsonOrderbookParser::HandleAsks},
      {"bids", &ThreadedJsonOrderbookParser::HandleBids},
      {"time", &ThreadedJsonOrderbookParser::HandleTime},
  }};

  std::vector<std::string_view> segments_;
  OrderbookDelta delta_;
};

int main() {

  std::ifstream file{test_path, std::ios::in};
  std::string contents{(std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>()};

  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << test_path << '\n';
    return 1;
  }

  // ── Equality test ──────────────────────────────────────────────────────
  // Both parsers, run on identical input, must produce the same delta. They
  // route the same char ranges through std::from_chars, so doubles compare
  // bitwise-equal — defaulted operator== on OrderbookEntry/OrderbookDelta is
  // sufficient. Verbose diff below if they ever diverge.
  ThreadedJsonOrderbookParser eq_threaded;
  JsonOrderbookParser eq_single;
  eq_threaded.Parse(contents);
  eq_single.Parse(contents);

  const auto &a = eq_threaded.delta();
  const auto &b = eq_single.delta();

  auto report_mismatch = [&] {
    std::println("EQUALITY: MISMATCH");
    if (a.sequence != b.sequence) {
      std::println("  sequence: threaded={} single={}", a.sequence, b.sequence);
    }
    if (a.bids_.size() != b.bids_.size()) {
      std::println("  bids size: threaded={} single={}", a.bids_.size(),
                   b.bids_.size());
    }
    if (a.asks_.size() != b.asks_.size()) {
      std::println("  asks size: threaded={} single={}", a.asks_.size(),
                   b.asks_.size());
    }
    const auto diff_n = [&](const char *side,
                            const std::vector<OrderbookEntry> &x,
                            const std::vector<OrderbookEntry> &y) {
      const u64 n = std::min(x.size(), y.size());
      int shown = 0;
      for (u64 i = 0; i < n && shown < 5; ++i) {
        if (!(x[i] == y[i])) {
          std::println(
              "  {}[{}] threaded: p={} s={} q={}   single: p={} s={} q={}",
              side, i, x[i].price, x[i].size, x[i].quantity, y[i].price,
              y[i].size, y[i].quantity);
          ++shown;
        }
      }
    };
    diff_n("bids", a.bids_, b.bids_);
    diff_n("asks", a.asks_, b.asks_);
  };

  if (a == b) {
    std::println("EQUALITY: OK  (seq={}, bids={}, asks={})", a.sequence,
                 a.bids_.size(), a.asks_.size());
  } else {
    report_mismatch();
  }

  using clock = std::chrono::steady_clock;
  constexpr int kWarmup = 100;
  constexpr int kIter = 1000;

  auto bench = [&](const char *label, auto run_once) {
    // Warmup: prime caches, branch predictors, allocator arenas.
    for (int i = 0; i < kWarmup; ++i)
      run_once();

    std::vector<i64> samples;
    samples.reserve(kIter);
    for (int i = 0; i < kIter; ++i) {
      const auto t0 = clock::now();
      run_once();
      const auto t1 = clock::now();
      samples.push_back(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
              .count());
    }

    std::ranges::sort(samples);
    const auto min = samples.front();
    const auto med = samples[samples.size() / 2];
    const auto p99 = samples[(samples.size() * 99) / 100];
    const auto max = samples.back();

    std::println(
        "{:<10} min={:>8} ns | med={:>8} ns | p99={:>8} ns | max={:>8} ns  "
        "({} iters, {} bytes)",
        label, min, med, p99, max, kIter, contents.size());
  };

  bench("threaded:", [&] {
    ThreadedJsonOrderbookParser p;
    p.Parse(contents);
  });

  bench("single:  ", [&] {
    JsonOrderbookParser p;
    p.Parse(contents);
  });

  return 0;
};
