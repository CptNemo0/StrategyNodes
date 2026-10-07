#ifndef DATA_FEED_KRAKEN_REST_ERROR_H_
#define DATA_FEED_KRAKEN_REST_ERROR_H_

#include <rapidjson/document.h>

#include <string_view>

namespace data_feed {

// Throws std::runtime_error prefixed with `context` if `document["error"]`
// is a non-empty array, per Kraken's REST error envelope.
void ThrowOnKrakenRestError(const rapidjson::Document& document,
                            std::string_view context);

}  // namespace data_feed

#endif  // ! DATA_FEED_KRAKEN_REST_ERROR_H_
