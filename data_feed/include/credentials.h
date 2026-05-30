#ifndef DATA_FEED_CREDENTIALS_H_
#define DATA_FEED_CREDENTIALS_H_

#include <memory>
#include <string>

namespace data_feed {

// Coinbase Exchange API credentials. The passphrase is optional: an empty
// passphrase authenticates successfully today, so only the key and secret are
// required.
class Credentials {
 public:
  // Reads COINBASE_API_KEY and COINBASE_API_SECRET (throwing std::runtime_error
  // naming whichever is missing) plus COINBASE_API_PASSPHRASE if present,
  // defaulting to an empty string otherwise.
  static std::unique_ptr<Credentials> FromEnvironment();

  const std::string& key() const { return key_; }
  const std::string& secret() const { return secret_; }
  const std::string& passphrase() const { return passphrase_; }

 private:
  Credentials(std::string key, std::string secret, std::string passphrase);

  std::string key_;
  std::string secret_;
  std::string passphrase_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_CREDENTIALS_H_
