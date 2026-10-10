#ifndef DATA_FEED_CORE_UNIX_TIME_H_
#define DATA_FEED_CORE_UNIX_TIME_H_

#include <optional>
#include <string_view>

#include "core/aliasing.h"

namespace core {

// The system clock as nanoseconds since the Unix epoch.
i64 UnixNanosNow();

// Parses an RFC 3339 UTC timestamp as Kraken sends it
// ("2026-10-04T18:32:21.811584Z") into nanoseconds since the Unix epoch.
// Fractional digits past nanosecond resolution are dropped. Returns nullopt
// if the text does not follow that layout.
std::optional<i64> ParseIso8601ToUnixNanos(std::string_view text);

}  // namespace core

#endif  // ! DATA_FEED_CORE_UNIX_TIME_H_
