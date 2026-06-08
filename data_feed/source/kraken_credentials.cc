#include "kraken_credentials.h"

#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "constants.h"
#include "utility.h"

namespace data_feed {

KrakenCredentials::KrakenCredentials(std::string public_key,
                                     std::string private_key)
    : public_key_(std::move(public_key)),
      private_key_(std::move(private_key)) {}

std::unique_ptr<KrakenCredentials> KrakenCredentials::FromEnvironment() {
  auto throw_if_empty =
      [](std::string_view variable_name) static -> std::string {
    if (auto [name, value] = DF_GetEnvironmentVariable(variable_name)) {
      return std::move(value.value());
    } else {
      throw std::runtime_error(
          std::format("Missing environment variable {}!", name));
    }
  };

  return std::make_unique<KrakenCredentials>(
      throw_if_empty(kKrakenApiKeyVarName),
      throw_if_empty(kKrakenPrivateKeyVarName));
}

}  // namespace data_feed
