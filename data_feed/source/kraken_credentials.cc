#include "kraken_credentials.h"

#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "constants.h"

namespace data_feed {

namespace {

using NameEnvVar = std::pair<std::string_view, std::optional<std::string>>;

NameEnvVar GetEnv(std::string_view name) {
#ifdef _WIN32
  // std::getenv is deprecated under the MSVC CRT; _dupenv_s is the sanctioned
  // replacement and hands back a private copy that we must free.
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name.data()) != 0 || value == nullptr) {
    return {name, std::nullopt};
  }
  std::string result(value);
  std::free(value);
  return {name, std::move(result)};
#else
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

}  // namespace

KrakenCredentials::KrakenCredentials(std::string public_key,
                                     std::string private_key)
    : public_key_(std::move(public_key)),
      private_key_(std::move(private_key)) {}

std::unique_ptr<KrakenCredentials> KrakenCredentials::FromEnvironment() {
  auto throw_if_empty = [](NameEnvVar&& name_value) static -> std::string {
    if (!name_value.second.has_value()) {
      throw std::runtime_error(
          std::format("Missing environment variable {}", name_value.first));
    }
    return std::move(name_value.second.value());
  };

  return std::unique_ptr<KrakenCredentials>{
      new KrakenCredentials(throw_if_empty(GetEnv(kKrakenApiKeyVarName)),
                            throw_if_empty(GetEnv(kKrakenPrivateKeyVarName)))};
}

}  // namespace data_feed
