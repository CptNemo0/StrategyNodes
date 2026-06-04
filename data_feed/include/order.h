#ifndef DATA_FEED_ORDER_H_
#define DATA_FEED_ORDER_H_

#include <format>
#include <string_view>

#include "aliasing.h"

struct Order;

struct Order {
  f64 price;
  f64 volume;
  id36 id;
};

template <>
struct std::formatter<Order> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  auto format(const Order& order, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "price: {}, volume: {}, id: {}",
                          order.price, order.volume,
                          std::string_view{order.id.data(), order.id.size()});
  }
};

#endif  // !DATA_FEED_ORDER_H_
