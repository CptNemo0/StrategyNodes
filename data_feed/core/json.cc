#include "core/json.h"

#include <rapidjson/document.h>

#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace core {

std::runtime_error MalformedJsonField(std::string_view name) {
  return std::runtime_error{
      std::format(R"(Malformed frame: missing or mistyped "{}")", name)};
}

std::optional<rapidjson::Value::ConstMemberIterator> FindJsonMember(
    const rapidjson::Value& object,
    std::string_view name) {
  if (!object.IsObject()) {
    return std::nullopt;
  }
  const rapidjson::Value::ConstMemberIterator member =
      object.FindMember(rapidjson::StringRef(
          name.data(), static_cast<rapidjson::SizeType>(name.size())));
  return member == object.MemberEnd() ? std::nullopt : std::optional{member};
}

const rapidjson::Value& RequireJsonMember(const rapidjson::Value& object,
                                          std::string_view name,
                                          IsJsonKind is_kind) {
  const std::optional<rapidjson::Value::ConstMemberIterator> member =
      FindJsonMember(object, name);
  if (!member || !((*member)->value.*is_kind)()) {
    throw MalformedJsonField(name);
  }
  return (*member)->value;
}

std::string_view RequireJsonString(const rapidjson::Value& object,
                                   std::string_view name) {
  const rapidjson::Value& value =
      RequireJsonMember(object, name, &rapidjson::Value::IsString);
  return {value.GetString(), value.GetStringLength()};
}

std::optional<std::string_view> FindJsonString(const rapidjson::Value& object,
                                               std::string_view name) {
  return FindJsonMember(object, name)
      .transform([name](rapidjson::Value::ConstMemberIterator member) {
        if (!member->value.IsString()) {
          throw MalformedJsonField(name);
        }
        return std::string_view{member->value.GetString(),
                                member->value.GetStringLength()};
      });
}

}  // namespace core
