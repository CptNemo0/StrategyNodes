#include "credentials.h"

#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>

namespace data_feed {

namespace {

std::optional<std::string> GetEnv(const char* name) {
#ifdef _WIN32
  // std::getenv is deprecated under the MSVC CRT; _dupenv_s is the sanctioned
  // replacement and hands back a private copy that we must free.
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

// Drops trailing CR/LF/space/tab, which silently corrupt the base64 secret (and
// thus the signature) when a value picks up a stray newline from how it was set.
std::string TrimTrailingWhitespace(std::string value) {
  const auto end = value.find_last_not_of(" \t\r\n");
  if (end == std::string::npos) {
    return {};
  }
  value.resize(end + 1);
  return value;
}

}  // namespace

Credentials::Credentials(std::string key,
                         std::string secret,
                         std::string passphrase)
    : key_(std::move(key)),
      secret_(std::move(secret)),
      passphrase_(std::move(passphrase)) {}

std::unique_ptr<Credentials> Credentials::FromEnvironment() {
  std::optional<std::string> key = GetEnv("COINBASE_API_KEY");
  if (!key.has_value()) {
    throw std::runtime_error(
        std::format("Missing environment variable {}", "COINBASE_API_KEY"));
  }

  std::optional<std::string> secret = GetEnv("COINBASE_API_SECRET");
  if (!secret.has_value()) {
    throw std::runtime_error(
        std::format("Missing environment variable {}", "COINBASE_API_SECRET"));
  }

  std::string passphrase = GetEnv("COINBASE_API_PASSPHRASE").value_or("");

  std::string trimmed_key = TrimTrailingWhitespace(std::move(*key));
  std::string trimmed_secret = TrimTrailingWhitespace(std::move(*secret));
  passphrase = TrimTrailingWhitespace(std::move(passphrase));

  return std::unique_ptr<Credentials>{new Credentials(
      std::move(trimmed_key), std::move(trimmed_secret), std::move(passphrase))};
}

}  // namespace data_feed
