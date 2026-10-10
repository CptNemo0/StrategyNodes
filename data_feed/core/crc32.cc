#include "core/crc32.h"

#include <array>
#include <string_view>

#include "core/aliasing.h"

namespace core {

namespace {

constexpr std::array<u32, 256> BuildCrc32Table() {
  std::array<u32, 256> table{};
  for (u32 value{}; value < table.size(); ++value) {
    u32 crc{value};
    for (u32 bit{}; bit < 8; ++bit) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    table[value] = crc;
  }
  return table;
}

}  // namespace

u32 Crc32(std::string_view data) {
  static constexpr std::array<u32, 256> kTable = BuildCrc32Table();

  u32 crc = 0xFFFFFFFFu;
  for (const unsigned char byte : data) {
    crc = kTable[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace core
