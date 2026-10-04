#include "l2_message_file_writer.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <ios>
#include <ranges>
#include <stdexcept>
#include <string>

#include "aliasing.h"
#include "l2_message.h"

namespace data_feed {

namespace {

i64 UnixTimeNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

L2MessageFileWriter::L2MessageFileWriter(const std::string& symbol)
    : file_stem_{std::format("{}-{}",
                             symbol | std::views::filter([](char c) {
                               return c != '/';
                             }) | std::ranges::to<std::string>(),
                             UnixTimeNow())},
      path_{file_stem_ + ".bin"},
      output_{path_, std::ios::binary} {
  if (!output_) {
    throw std::runtime_error{std::format("Cannot open {}", path_.string())};
  }
}

L2MessageFileWriter::~L2MessageFileWriter() {
  try {
    Close();
  } catch (...) {
    // A failed rename leaves the data under the recording name.
  }
}

void L2MessageFileWriter::Write(const L2Message& message) {
  buffer_.resize(message.GetByteSize());
  output_.write(reinterpret_cast<const char*>(buffer_.data()),
                static_cast<std::streamsize>(message.Serialize(buffer_)));
  output_.flush();
}

void L2MessageFileWriter::Close() {
  if (!output_.is_open()) {
    return;
  }
  output_.close();
  std::filesystem::rename(path_,
                          std::format("{}-{}.bin", file_stem_, UnixTimeNow()));
}

}  // namespace data_feed
