#ifndef DATA_FEED_KRAKEN_CREDENTIALS_H_
#define DATA_FEED_KRAKEN_CREDENTIALS_H_

#include <memory>
#include <string>
#include <string_view>

namespace kraken {

class Credentials {
 public:
  static std::unique_ptr<Credentials> FromEnvironment();

  Credentials(std::string public_key, std::string private_key);

  std::string_view public_key() const { return public_key_; }
  std::string_view private_key() const { return private_key_; }

 private:
  std::string public_key_;
  std::string private_key_;
};

}  // namespace kraken

#endif  // !DATA_FEED_KRAKEN_CREDENTIALS_H_
