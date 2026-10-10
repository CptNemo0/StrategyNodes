#include "l2/file_writer.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <ios>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "core/aliasing.h"
#include "core/spsc.h"
#include "core/unix_time.h"
#include "l2/file_header.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace l2 {

namespace {

constexpr i64 kNanosPerSecond{1'000'000'000};

i64 UnixTimeNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

FileWriter::FileWriter(std::string_view venue,
                       const std::string& symbol,
                       u32 depth,
                       const PairPrecision& precision)
    : start_time_ns_{core::UnixNanosNow()},
      file_stem_{std::format("{}-{}",
                             symbol | std::views::filter([](char c) {
                               return c != '/';
                             }) | std::ranges::to<std::string>(),
                             start_time_ns_ / kNanosPerSecond)},
      path_{file_stem_ + ".bin"},
      output_{path_, std::ios::binary} {
  if (!output_) {
    throw std::runtime_error{std::format("Cannot open {}", path_.string())};
  }

  const FileHeader header =
      MakeFileHeader(venue, symbol, depth, precision, start_time_ns_);
  output_.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output_.flush();
  if (!output_) {
    throw std::runtime_error{
        std::format("Cannot write header to {}", path_.string())};
  }

  thread_ = std::jthread{[this, symbol](const std::stop_token& stop) {
    core::Drain(
        stop, symbol, queue_,
        [this](const Message& message) { Write(message); },
        [this] { failed_ = true; });
  }};
}

FileWriter::~FileWriter() {
  thread_.request_stop();
  thread_.join();
  try {
    Close();
  } catch (...) {
    // A failed rename leaves the data under the recording name.
  }
}

void FileWriter::Receive(Message message) {
  core::PushBlocking(queue_, std::move(message), failed_);
}

void FileWriter::Write(const Message& message) {
  buffer_.resize(message.GetByteSize());
  output_.write(reinterpret_cast<const char*>(buffer_.data()),
                static_cast<std::streamsize>(message.Serialize(buffer_)));
  output_.flush();
  if (!output_) {
    throw std::runtime_error{std::format("Cannot write to {}", path_.string())};
  }
}

void FileWriter::Close() {
  if (!output_.is_open()) {
    return;
  }
  output_.close();
  std::filesystem::rename(path_,
                          std::format("{}-{}.bin", file_stem_, UnixTimeNow()));
}

}  // namespace l2
