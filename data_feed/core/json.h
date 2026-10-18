#ifndef DATA_FEED_CORE_JSON_H_
#define DATA_FEED_CORE_JSON_H_

#include <rapidjson/document.h>

#include <optional>
#include <stdexcept>
#include <string_view>

namespace core {

// Lookups into a rapidjson tree that throw on a layout they do not expect
// rather than letting rapidjson assert, so a frame whose layout a venue
// changed fails the pipeline instead of aborting the process.

using IsJsonKind = bool (rapidjson::Value::*)() const;

// Thrown for a member that is missing or of the wrong kind.
std::runtime_error MalformedJsonField(std::string_view name);

// The member `name` of `object`, if `object` is an object that has it.
std::optional<rapidjson::Value::ConstMemberIterator> FindJsonMember(
    const rapidjson::Value& object,
    std::string_view name);

// The member `name` of `object`, which must be of the kind `is_kind` checks
// for.
const rapidjson::Value& RequireJsonMember(const rapidjson::Value& object,
                                          std::string_view name,
                                          IsJsonKind is_kind);

// The text of the member `name` of `object`, which must be a string.
std::string_view RequireJsonString(const rapidjson::Value& object,
                                   std::string_view name);

// The text of the optional member `name` of `object`, which must be a string
// if present.
std::optional<std::string_view> FindJsonString(const rapidjson::Value& object,
                                               std::string_view name);

}  // namespace core

#endif  // ! DATA_FEED_CORE_JSON_H_
