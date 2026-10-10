#include "kraken/credentials.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "kraken/constants.h"

namespace kraken {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::ThrowsMessage;

void SetEnvironmentVariable(const std::string& name,
                            const std::optional<std::string>& value) {
#ifdef _WIN32
  // An empty value removes the variable on Windows.
  _putenv_s(name.c_str(), value.value_or("").c_str());
#else
  value ? setenv(name.c_str(), value->c_str(), 1) : unsetenv(name.c_str());
#endif
}

// Overrides an environment variable (std::nullopt unsets it) for the lifetime
// of the object, then restores whatever was there, so a developer's real
// credentials survive the test run.
class ScopedEnvironmentOverride {
 public:
  ScopedEnvironmentOverride(std::string_view name,
                            const std::optional<std::string>& value)
      : name_{name}, original_{[this] -> std::optional<std::string> {
          const char* original = std::getenv(name_.c_str());
          return original == nullptr ? std::nullopt
                                     : std::optional<std::string>{original};
        }()} {
    SetEnvironmentVariable(name_, value);
  }

  ScopedEnvironmentOverride(const ScopedEnvironmentOverride&) = delete;
  ScopedEnvironmentOverride& operator=(const ScopedEnvironmentOverride&) =
      delete;

  ~ScopedEnvironmentOverride() { SetEnvironmentVariable(name_, original_); }

 private:
  std::string name_;
  std::optional<std::string> original_;
};

void LoadFromEnvironment() {
  Credentials::FromEnvironment();
}

// The keys given to the constructor are handed back unchanged.
TEST(CredentialsTest, HoldsTheKeysItIsGiven) {
  const Credentials credentials{"public", "private"};

  EXPECT_EQ(credentials.public_key(), "public");
  EXPECT_EQ(credentials.private_key(), "private");
}

// FromEnvironment() reads the API key and the private key from their own
// KRAKEN_* variables, without mixing them up.
TEST(CredentialsTest, LoadsBothKeysFromTheEnvironment) {
  const ScopedEnvironmentOverride api_key{kApiKeyVarName, "env public"};
  const ScopedEnvironmentOverride private_key{kPrivateKeyVarName,
                                              "env private"};

  EXPECT_THAT(
      Credentials::FromEnvironment(),
      Pointee(AllOf(Property(&Credentials::public_key, "env public"),
                    Property(&Credentials::private_key, "env private"))));
}

// A missing API key fails loudly, naming the variable to set.
TEST(CredentialsTest, NamesTheMissingApiKey) {
  const ScopedEnvironmentOverride api_key{kApiKeyVarName, std::nullopt};
  const ScopedEnvironmentOverride private_key{kPrivateKeyVarName,
                                              "env private"};

  EXPECT_THAT(LoadFromEnvironment, ThrowsMessage<std::runtime_error>(
                                       HasSubstr(std::string{kApiKeyVarName})));
}

// A missing private key fails loudly, naming the variable to set.
TEST(CredentialsTest, NamesTheMissingPrivateKey) {
  const ScopedEnvironmentOverride api_key{kApiKeyVarName, "env public"};
  const ScopedEnvironmentOverride private_key{kPrivateKeyVarName, std::nullopt};

  EXPECT_THAT(LoadFromEnvironment, ThrowsMessage<std::runtime_error>(HasSubstr(
                                       std::string{kPrivateKeyVarName})));
}

}  // namespace
}  // namespace kraken
