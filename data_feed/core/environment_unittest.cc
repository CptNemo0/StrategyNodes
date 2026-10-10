#include "core/environment.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace core {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Optional;

constexpr std::string_view kVariableName = "DATA_FEED_ENVIRONMENT_UNITTEST";

// Sets an environment variable for the lifetime of the object, then removes
// it, so no test leaks state into the next one.
class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_{std::move(name)} {
    Set(value);
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) =
      delete;

  ~ScopedEnvironmentVariable() { Unset(); }

 private:
  void Set(const std::string& value) const {
#ifdef _WIN32
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  void Unset() const {
#ifdef _WIN32
    // An empty value removes the variable on Windows.
    _putenv_s(name_.c_str(), "");
#else
    unsetenv(name_.c_str());
#endif
  }

  std::string name_;
};

// A set variable is reported as present, under its name, with its value.
TEST(ReadEnvironmentVariableTest, ReturnsTheValueOfASetVariable) {
  const ScopedEnvironmentVariable variable{std::string{kVariableName},
                                           "some value"};

  const EnvironmentVarNameValue result = ReadEnvironmentVariable(kVariableName);

  EXPECT_TRUE(result);
  EXPECT_THAT(result, AllOf(Field("name", &EnvironmentVarNameValue::name,
                                  kVariableName),
                            Field("value", &EnvironmentVarNameValue::value,
                                  Optional(std::string{"some value"}))));
}

// A missing variable is reported as absent, still carrying its name for
// error messages.
TEST(ReadEnvironmentVariableTest, ReportsAMissingVariableByName) {
  const EnvironmentVarNameValue result = ReadEnvironmentVariable(kVariableName);

  EXPECT_FALSE(result);
  EXPECT_THAT(
      result,
      AllOf(Field("name", &EnvironmentVarNameValue::name, kVariableName),
            Field("value", &EnvironmentVarNameValue::value, Eq(std::nullopt))));
}

// Reads reflect the current environment, not a cached copy.
TEST(ReadEnvironmentVariableTest, SeesAVariableOnlyWhileItIsSet) {
  {
    const ScopedEnvironmentVariable variable{std::string{kVariableName}, "x"};
    EXPECT_TRUE(ReadEnvironmentVariable(kVariableName));
  }
  EXPECT_FALSE(ReadEnvironmentVariable(kVariableName));
}

// Values come back byte for byte, including base64 punctuation.
TEST(ReadEnvironmentVariableTest, PreservesTheValueVerbatim) {
  // Base64 secrets carry '+', '/' and '='.
  constexpr std::string_view kSecret =
      "kQH5HW/8p1uGOVjbgWA7FunAmGO8lsSUXNsu3eow"
      "76sz84Q18fWxnyRzBHCd3pd5nE9qa99HAZtuZu"
      "j6mDRnSw==";
  const ScopedEnvironmentVariable variable{std::string{kVariableName},
                                           std::string{kSecret}};

  EXPECT_THAT(ReadEnvironmentVariable(kVariableName).value,
              Optional(std::string{kSecret}));
}

}  // namespace
}  // namespace core
