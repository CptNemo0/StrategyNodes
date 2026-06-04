#ifndef DATA_FEED_CREDENTIALS_H_
#define DATA_FEED_CREDENTIALS_H_

#include <memory>
#include <string>
#include <string_view>

namespace data_feed {

class KrakenCredentials {
 public:
  static std::unique_ptr<KrakenCredentials> FromEnvironment();

  KrakenCredentials(std::string public_key, std::string private_key);

  std::string_view public_key() const { return public_key_; }
  std::string_view private_key() const { return private_key_; }

 private:
  std::string public_key_;
  std::string private_key_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_CREDENTIALS_H_
