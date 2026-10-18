#include "l2/file_writer.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <ios>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "core/aliasing.h"
#include "core/debug_log.h"
#include "core/receiver.h"
#include "core/spsc.h"
#include "core/unix_time.h"
#include "l2/book_spec.h"
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

// `[stem]-[end].bin`, or `[stem]-[end]-[n].bin` for the first free n, so a
// recording never replaces one that started and ended in the same seconds.
std::filesystem::path FinalPath(std::string_view stem, i64 end) {
  std::filesystem::path path = std::format("{}-{}.bin", stem, end);
  for (int n = 1; std::filesystem::exists(path); ++n) {
    path = std::format("{}-{}-{}.bin", stem, end, n);
  }
  return path;
}

}  // namespace

FileWriter::FileWriter(std::string_view venue,
                       const BookSpec& book,
                       const PairPrecision& precision)
    : symbol_stem_{book.symbol |
                   std::views::filter([](char c) { return c != '/'; }) |
                   std::ranges::to<std::string>()},
      header_{MakeFileHeader(venue, book, precision, /*start_time_ns=*/0)} {
  Open();
  thread_ = std::jthread{[this,
                          symbol = book.symbol](const std::stop_token& stop) {
    core::Drain(
        stop, symbol, queue_, [this](const Entry& entry) { Consume(entry); },
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
  core::PushBlocking(queue_, Entry{std::move(message)}, failed_);
}

void FileWriter::OnGap(core::Gap /*gap*/) {
  core::PushBlocking(queue_, Entry{std::nullopt}, failed_);
}

void FileWriter::Consume(const Entry& entry) {
  if (!entry) {
    // An empty file is kept for the next connection.
    if (messages_in_file_ > 0) {
      Close();
    }
    return;
  }
  if (!output_.is_open()) {
    Open();
  }
  Write(*entry);
}

void FileWriter::Open() {
  header_.start_time_ns = core::UnixNanosNow();
  file_stem_ = std::format("{}-{}", symbol_stem_,
                           header_.start_time_ns / kNanosPerSecond);
  path_ = file_stem_ + ".bin";
  output_.clear();
  output_.open(path_, std::ios::binary);
  if (!output_) {
    throw std::runtime_error{std::format("Cannot open {}", path_.string())};
  }
  output_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
  output_.flush();
  if (!output_) {
    throw std::runtime_error{
        std::format("Cannot write header to {}", path_.string())};
  }
  messages_in_file_ = 0;
  DEBUG_LOG("Recording to {}", path_.string());
}

void FileWriter::Write(const Message& message) {
  buffer_.resize(message.GetByteSize());
  output_.write(reinterpret_cast<const char*>(buffer_.data()),
                static_cast<std::streamsize>(message.Serialize(buffer_)));
  output_.flush();
  if (!output_) {
    throw std::runtime_error{std::format("Cannot write to {}", path_.string())};
  }
  ++messages_in_file_;
}

void FileWriter::Close() {
  if (!output_.is_open()) {
    return;
  }
  output_.close();
  const std::filesystem::path final_path = FinalPath(file_stem_, UnixTimeNow());
  std::filesystem::rename(path_, final_path);
  DEBUG_LOG("Closed {} after {} messages", final_path.string(),
            messages_in_file_);
}

}  // namespace l2
