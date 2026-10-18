#include "coinbase/products.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#include "l2/pair_precision.h"

namespace coinbase {
namespace {

using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::Throws;
using ::testing::ThrowsMessage;

// The fields of a public product that decide whether and how it is streamed.
struct Product {
  std::string_view id = "BTC-USD";
  std::string_view status = "online";
  bool trading_disabled = false;
  std::string_view alias;
  std::string_view quote_increment = "0.01";
  std::string_view price_increment = "0.01";
  std::string_view base_increment = "0.00000001";

  std::string ToJson() const {
    return std::format(
        R"({{"product_id":"{}","status":"{}","is_disabled":false,)"
        R"("trading_disabled":{},"alias":"{}","quote_increment":"{}",)"
        R"("price_increment":"{}","base_increment":"{}","product_type":"SPOT"}})",
        id, status, trading_disabled, alias, quote_increment, price_increment,
        base_increment);
  }
};

// The digits of the increments, trailing zeros aside, are the precision.
TEST(ParseProductPrecisionTest, CountsTheIncrementsFractionDigits) {
  EXPECT_THAT(ParseProductPrecision(Product{.price_increment = "0.01000000",
                                            .base_increment = "0.00000001"}
                                        .ToJson()),
              FieldsAre(2u, 8u));
  EXPECT_THAT(
      ParseProductPrecision(
          Product{.price_increment = "0.5", .base_increment = "1"}.ToJson()),
      FieldsAre(1u, 0u));
}

// Without a price increment, the quote increment is the price step.
TEST(ParseProductPrecisionTest, FallsBackToTheQuoteIncrement) {
  EXPECT_THAT(
      ParseProductPrecision(
          Product{.quote_increment = "0.001", .price_increment = ""}.ToJson()),
      FieldsAre(3u, 8u));
}

// A product that cannot be streamed is refused as unknown, saying why: one
// not online, one Coinbase stopped trading, and an alias, whose data comes
// under the product it aliases.
TEST(ParseProductPrecisionTest, RefusesAProductThatCannotBeStreamed) {
  EXPECT_THAT(
      [] { ParseProductPrecision(Product{.status = "delisted"}.ToJson()); },
      ThrowsMessage<UnknownProductError>(HasSubstr("delisted")));
  EXPECT_THAT(
      [] { ParseProductPrecision(Product{.trading_disabled = true}.ToJson()); },
      ThrowsMessage<UnknownProductError>(HasSubstr("disabled")));
  EXPECT_THAT(
      [] {
        ParseProductPrecision(
            Product{.id = "BTC-USDC", .alias = "BTC-USD"}.ToJson());
      },
      ThrowsMessage<UnknownProductError>(HasSubstr("BTC-USD;")));
}

// A body that is no product is a venue failure, not an unknown symbol.
TEST(ParseProductPrecisionTest, ThrowsOnABodyThatIsNoProduct) {
  EXPECT_THAT([] { ParseProductPrecision("<html>Bad gateway</html>"); },
              Throws<std::runtime_error>(Property(&std::runtime_error::what,
                                                  HasSubstr("Bad gateway"))));
  EXPECT_THAT([] { ParseProductPrecision(R"({"status":"online"})"); },
              Throws<std::runtime_error>());
}

// The list keeps only products that can be streamed, sorted by id.
TEST(ParseTradableProductsTest, KeepsTheStreamableProductsSorted) {
  EXPECT_THAT(
      ParseTradableProducts(std::format(
          R"({{"products":[{},{},{},{},{}],"num_products":5}})",
          Product{.id = "SOL-USD"}.ToJson(), Product{.id = "BTC-USD"}.ToJson(),
          Product{.id = "OLD-USD", .status = "delisted"}.ToJson(),
          Product{.id = "BTC-USDC", .alias = "BTC-USD"}.ToJson(),
          Product{.id = "HALT-USD", .trading_disabled = true}.ToJson())),
      ElementsAre("BTC-USD", "SOL-USD"));
  EXPECT_THAT(ParseTradableProducts(R"({"products":[]})"), IsEmpty());
}

TEST(ParseTradableProductsTest, ThrowsOnABodyThatIsNoProductList) {
  EXPECT_THAT([] { ParseTradableProducts(R"({"error":"unavailable"})"); },
              Throws<std::runtime_error>());
}

}  // namespace
}  // namespace coinbase
