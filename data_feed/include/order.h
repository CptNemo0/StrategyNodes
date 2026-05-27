#ifndef DATA_FEED_ORDER_H_
#define DATA_FEED_ORDER_H_

#include "aliasing.h"

struct Order {
  f64 price;
  f64 volume;
  id31 id;
};

#endif // !DATA_FEED_ORDER_H_
