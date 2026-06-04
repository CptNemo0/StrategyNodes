#ifndef DATA_FEED_CONFIG_MANAGER_H_
#define DATA_FEED_CONFIG_MANAGER_H_

namespace data_feed {

class ConfigManager {
 public:
  ConfigManager& Instance() {
    static ConfigManager instance;
    return instance;
  }

  ~ConfigManager() = default;

  ConfigManager(const ConfigManager&) = delete ("Singleton");
  ConfigManager(ConfigManager&&) = delete ("Singleton");
  void opeartor(const ConfigManager&) = delete ("Singleton");
  void opeartor(ConfigManager&&) = delete ("Singleton");

 private:
  ConfigManager() = default;
  void Init();
};

}  // namespace data_feed

#endif  // ! DATA_FEED_CONFIG_MANAGER_H_
