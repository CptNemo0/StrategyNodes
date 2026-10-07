#include "kraken_rest_error.h"

#include <cassert>
#include <format>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace data_feed {

void ThrowOnKrakenRestError(const rapidjson::Document& document,
                            std::string_view context) {
  const rapidjson::Value& error_array = document["error"];
  assert(error_array.IsArray());
  if (error_array.Empty()) {
    return;
  }

  std::stringstream error_stream{};
  error_stream << context;
  for (auto i{0uz}; i < error_array.Size(); ++i) {
    error_stream << (error_array[i].IsString()
                         ? std::format("\t{}\n", error_array[i].GetString())
                         : "\t[empty error field]\n");
  }
  throw std::runtime_error{std::move(error_stream).str()};
}

}  // namespace data_feed
