#ifndef DATA_FEED_CORE_DEBUG_LOG_H_
#define DATA_FEED_CORE_DEBUG_LOG_H_

#include <chrono>
#include <cstdio>
#include <format>
#include <print>
#include <source_location>
#include <string_view>
#include <thread>
#include <utility>

namespace core {

// Release builds define NDEBUG, Debug builds do not.
#ifdef NDEBUG
inline constexpr bool kDebugLogging = false;
#else
inline constexpr bool kDebugLogging = true;
#endif

namespace internal {

// One line on stderr: `[UTC time, thread, file:line] message`. Written in a
// single call, so lines from different threads do not interleave.
template <typename... Args>
void DebugLog(const std::source_location& location,
              std::format_string<Args...> format,
              Args&&... args) {
  const std::string_view file = location.file_name();
  std::println(stderr, "[{:%T} {} {}:{}] {}",
               std::chrono::floor<std::chrono::microseconds>(
                   std::chrono::system_clock::now()),
               std::this_thread::get_id(),
               file.substr(file.find_last_of("/\\") + 1), location.line(),
               std::format(format, std::forward<Args>(args)...));
}

}  // namespace internal

}  // namespace core

// Takes std::format arguments. A macro rather than a function so a Release
// build does not evaluate the arguments either, while still checking the
// format string against them, so a log line cannot break only one build.
#define DEBUG_LOG(...)                                            \
  do {                                                            \
    if constexpr (::core::kDebugLogging) {                        \
      ::core::internal::DebugLog(std::source_location::current(), \
                                 __VA_ARGS__);                    \
    }                                                             \
  } while (false)

#endif  // ! DATA_FEED_CORE_DEBUG_LOG_H_
