#include "l2/message.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "core/aliasing.h"

namespace l2 {

namespace {

static_assert(std::is_trivially_copyable_v<Message::Level>);
static_assert(sizeof(Message::Level) == 2 * sizeof(i64),
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

// Copies one trivially copyable scalar and returns the rest of out.
template <typename T>
std::span<std::byte> WriteValue(std::span<std::byte> out, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  return Write(out, &value, sizeof(value));
}

std::span<std::byte> WriteSide(std::span<std::byte> out,
                               const std::vector<Message::Level>& side) {
  const u64 byte_size = side.size() * sizeof(Message::Level);
  return Write(WriteValue(out, byte_size), side.data(), byte_size);
}

}  // namespace

u64 Message::Serialize(std::span<std::byte> out) const {
  if (out.size() < GetByteSize()) {
    throw std::length_error{"l2::Message::Serialize: output buffer too small"};
  }

  // Written one field at a time rather than nested, so the on-disk order is
  // readable top to bottom and matches the layout comment in the header.
  std::span<std::byte> rest = WriteValue(out, type);
  rest = WriteValue(rest, checksum);
  rest = WriteValue(rest, venue_time_ns);
  rest = WriteValue(rest, capture_time_ns);
  WriteSide(WriteSide(rest, buys), sells);

  return GetByteSize();
}

}  // namespace l2
