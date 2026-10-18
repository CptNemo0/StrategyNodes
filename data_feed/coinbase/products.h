#ifndef DATA_FEED_COINBASE_PRODUCTS_H_
#define DATA_FEED_COINBASE_PRODUCTS_H_

#include <string>
#include <string_view>
#include <vector>

#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace coinbase {

// Thrown when Coinbase does not list a product, or lists it but it cannot be
// streamed, so a caller can tell a mistyped symbol from a venue that cannot be
// reached.
class UnknownProductError : public venue::UnknownSymbolError {
 public:
  using venue::UnknownSymbolError::UnknownSymbolError;
};

// The precision of one product, parsed from Coinbase's public product
// endpoint's `body`: the fraction digits of price_increment (or of
// quote_increment, if it has none) and of base_increment. Throws
// UnknownProductError if the product is not online, cannot be traded, or is
// an alias of another product (whose id the public channels send its data
// under), and std::runtime_error if `body` is not a product.
l2::PairPrecision ParseProductPrecision(std::string_view body);

// The ids of the products in Coinbase's public products endpoint's `body`
// that are online, can be traded and are no alias, sorted. Throws
// std::runtime_error if `body` is not a product list.
std::vector<std::string> ParseTradableProducts(std::string_view body);

// Queries Coinbase's public product endpoint for `product_id` (e.g.
// "BTC-USD") and parses it with ParseProductPrecision(). Throws
// UnknownProductError if Coinbase does not list it, and std::runtime_error
// for any other failure.
l2::PairPrecision FetchProductPrecision(std::string_view product_id);

// Queries Coinbase's public products endpoint for every spot product and
// parses it with ParseTradableProducts().
std::vector<std::string> FetchTradableProducts();

}  // namespace coinbase

#endif  // ! DATA_FEED_COINBASE_PRODUCTS_H_
