#ifndef DATA_FEED_ORDERBOOK_DELTA_H_
#define DATA_FEED_ORDERBOOK_DELTA_H_

#include <format>
#include <vector>

#include "aliasing.h"
#include "order.h"

struct OrderbookDelta;

template <>
struct std::formatter<OrderbookDelta>;

struct OrderbookDelta {
  alignas(64) std::vector<Order> asks_;
  alignas(64) std::vector<Order> bids_;
  alignas(64) u64 sequence;

  bool operator==(const OrderbookDelta&) const = default;

  // Will truncate the output of ONLY THE NEXT call to
  // std::print/std::println/std::format to first 50 bids and first 50 asks.
  const OrderbookDelta& trunc() const {
    truncated_ = true;
    return *this;
  }

 private:
  friend class std::formatter<OrderbookDelta>;
  mutable bool truncated_{false};
};

template <>
struct std::formatter<OrderbookDelta> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  auto format(const OrderbookDelta& delta, std::format_context& ctx) const {
    auto out = std::format_to(ctx.out(), "sequence: {}\n", delta.sequence);

    out = std::format_to(out, "bids ({}):\n", delta.bids_.size());

    u64 i = 0;
    for (const Order& bid : delta.bids_) {
      out = std::format_to(out, "  {}\n", bid);
      if (++i == 50) {
        break;
      }
    }

    out = std::format_to(out, "asks ({}):\n", delta.asks_.size());
    for (const Order& ask : delta.asks_) {
      out = std::format_to(out, "  {}\n", ask);
      if (++i == 100) {
        break;
      }
    }

    delta.truncated_ = false;
    return out;
  }
};

#endif  //! DATA_FEED_ORDERBOOK_DELTA_H_
