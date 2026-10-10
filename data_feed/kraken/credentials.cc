#include "kraken/credentials.h"

#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "core/environment.h"
#include "kraken/constants.h"

namespace kraken {

Credentials::Credentials(std::string public_key, std::string private_key)
    : public_key_(std::move(public_key)),
      private_key_(std::move(private_key)) {}

std::unique_ptr<Credentials> Credentials::FromEnvironment() {
  auto throw_if_empty =
      [](std::string_view variable_name) static -> std::string {
    if (auto [name, value] = core::ReadEnvironmentVariable(variable_name)) {
      return std::move(value.value());
    } else {
      throw std::runtime_error(
          std::format("Missing environment variable {}!", name));
    }
  };

  return std::make_unique<Credentials>(throw_if_empty(kApiKeyVarName),
                                       throw_if_empty(kPrivateKeyVarName));
}

}  // namespace kraken
