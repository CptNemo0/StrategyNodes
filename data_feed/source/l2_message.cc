#include "l2_message.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "aliasing.h"

namespace data_feed {

namespace {

static_assert(std::is_trivially_copyable_v<L2Message::Level>);
static_assert(sizeof(L2Message::Level) == 2 * sizeof(i64),
              "Level must have no padding to be copied as raw bytes");

// Copies size bytes from source to the front of out and returns the rest.
// An empty side has a null data(), which memcpy must not receive.
std::span<std::byte> Write(std::span<std::byte> out,
                           const void* source,
                           std::size_t size) {
  if (size == 0) {
    return out;
  }
  std::memcpy(out.data(), source, size);
  return out.subspan(size);
}

std::span<std::byte> WriteSide(std::span<std::byte> out,
                               const std::vector<L2Message::Level>& side) {
  const u64 byte_size = side.size() * sizeof(L2Message::Level);
  return Write(Write(out, &byte_size, sizeof(byte_size)), side.data(),
               byte_size);
}

}  // namespace

u64 L2Message::Serialize(std::span<std::byte> out) const {
  if (out.size() < GetByteSize()) {
    throw std::length_error{"L2Message::Serialize: output buffer too small"};
  }

  WriteSide(WriteSide(Write(out, &type, sizeof(type)), buys), sells);
  return GetByteSize();
}

}  // namespace data_feed
