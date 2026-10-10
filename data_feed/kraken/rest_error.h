#ifndef DATA_FEED_KRAKEN_REST_ERROR_H_
#define DATA_FEED_KRAKEN_REST_ERROR_H_

#include <rapidjson/document.h>

#include <string_view>

namespace kraken {

// Throws std::runtime_error prefixed with `context` if `document["error"]`
// is a non-empty array, per Kraken's REST error envelope.
void ThrowOnRestError(const rapidjson::Document& document,
                      std::string_view context);

}  // namespace kraken

#endif  // ! DATA_FEED_KRAKEN_REST_ERROR_H_
