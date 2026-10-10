#include "l2_message_file_writer.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <ios>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

#include "aliasing.h"
#include "constants.h"
#include "kraken_pair_precision.h"
#include "l2_file_header.h"
#include "l2_message.h"
#include "spsc_util.h"
#include "utility.h"

namespace data_feed {

namespace {

constexpr i64 kNanosPerSecond{1'000'000'000};

i64 UnixTimeNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

L2MessageFileWriter::L2MessageFileWriter(const std::string& symbol,
                                         u32 depth,
                                         const PairPrecision& precision)
    : start_time_ns_{UnixNanosNow()},
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

  const L2FileHeader header =
      MakeL2FileHeader(kVenueKraken, symbol, depth, precision, start_time_ns_);
  output_.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output_.flush();
  if (!output_) {
    throw std::runtime_error{
        std::format("Cannot write header to {}", path_.string())};
  }

  thread_ = std::jthread{[this, symbol](const std::stop_token& stop) {
    Drain(
        stop, symbol, queue_,
        [this](const L2Message& message) { Write(message); },
        [this] { failed_ = true; });
  }};
}

L2MessageFileWriter::~L2MessageFileWriter() {
  thread_.request_stop();
  thread_.join();
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
  if (!output_) {
    throw std::runtime_error{std::format("Cannot write to {}", path_.string())};
  }
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
