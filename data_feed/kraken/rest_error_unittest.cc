#include "kraken/rest_error.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace kraken {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::StartsWith;
using ::testing::ThrowsMessage;

constexpr std::string_view kContext = "Errors when testing: ";

// Checks the response `json` as a REST call would. A callable, so matchers
// can tell what it throws.
auto CheckResponse(std::string_view json) {
  return [json] {
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    ThrowOnRestError(document, kContext);
  };
}

// A response with an empty "error" array is a success and passes through.
TEST(ThrowOnRestErrorTest, AcceptsAResponseWithoutErrors) {
  EXPECT_NO_THROW(CheckResponse(R"({"error":[],"result":{"token":"abc"}})")());
}

// The reported error carries the caller's context first, then Kraken's error
// text, so the log says which request failed and why.
TEST(ThrowOnRestErrorTest, ReportsAnErrorUnderTheGivenContext) {
  EXPECT_THAT(CheckResponse(R"({"error":["EGeneral:Invalid arguments"]})"),
              ThrowsMessage<std::runtime_error>(
                  AllOf(StartsWith(std::string{kContext}),
                        HasSubstr("EGeneral:Invalid arguments"))));
}

// Kraken may report several errors at once; none of them is dropped.
TEST(ThrowOnRestErrorTest, ReportsEveryError) {
  EXPECT_THAT(
      CheckResponse(
          R"({"error":["EAPI:Invalid key","EAPI:Invalid nonce"],"result":{}})"),
      ThrowsMessage<std::runtime_error>(AllOf(
          HasSubstr("EAPI:Invalid key"), HasSubstr("EAPI:Invalid nonce"))));
}

// An error entry that is not a string still fails the request, with a
// placeholder in place of its text.
TEST(ThrowOnRestErrorTest, ReportsAnErrorThatIsNotAString) {
  EXPECT_THAT(
      CheckResponse(R"({"error":[42]})"),
      ThrowsMessage<std::runtime_error>(HasSubstr("[empty error field]")));
}

}  // namespace
}  // namespace kraken
