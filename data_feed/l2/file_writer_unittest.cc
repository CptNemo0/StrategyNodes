#include "l2/file_writer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "core/unix_time.h"
#include "l2/file_header.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace l2 {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Ge;
using ::testing::Le;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::Test;
using ::testing::UnorderedElementsAre;

constexpr std::string_view kVenue = "kraken";
constexpr u32 kDepth = 25;
constexpr PairPrecision kPrecision{.price_decimals = 1, .qty_decimals = 8};
constexpr i64 kNanosPerSecond{1'000'000'000};

// FileWriter writes into the working directory, so each test runs in a fresh
// directory of its own, removed afterwards.
class FileWriterTest : public Test {
 protected:
  void SetUp() override {
    original_directory_ = std::filesystem::current_path();
    directory_ =
        std::filesystem::temp_directory_path() /
        std::format("data_feed_file_writer_unittest-{}", core::UnixNanosNow());
    std::filesystem::create_directories(directory_);
    std::filesystem::current_path(directory_);
  }

  void TearDown() override {
    std::filesystem::current_path(original_directory_);
    std::filesystem::remove_all(directory_);
  }

  // Every recording in the directory, by file name, sorted.
  static std::vector<std::string> RecordingNames() {
    std::vector<std::string> names =
        std::filesystem::directory_iterator{"."} |
        std::views::transform([](const std::filesystem::directory_entry& e) {
          return e.path().filename().string();
        }) |
        std::views::filter(
            [](const std::string& name) { return name.ends_with(".bin"); }) |
        std::ranges::to<std::vector>();
    std::ranges::sort(names);
    return names;
  }

  // The only recording in the directory; fails the test if there is not
  // exactly one.
  static std::optional<std::string> OnlyRecordingName() {
    const std::vector<std::string> names = RecordingNames();
    EXPECT_THAT(names, SizeIs(1));
    return names.size() == 1 ? std::optional{names.front()} : std::nullopt;
  }

  static std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::views::istream<char>(input >> std::noskipws) |
           std::views::transform([](char c) { return std::byte(c); }) |
           std::ranges::to<std::vector>();
  }

  static FileHeader ReadHeader(const std::vector<std::byte>& bytes) {
    FileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    return header;
  }

  // What follows the header.
  static std::span<const std::byte> Records(
      const std::vector<std::byte>& bytes) {
    return std::span{bytes}.subspan(sizeof(FileHeader));
  }

  static std::vector<std::byte> HeaderBytes(const FileHeader& header) {
    std::vector<std::byte> bytes(sizeof(header));
    std::memcpy(bytes.data(), &header, sizeof(header));
    return bytes;
  }

  static Message MakeUpdate(i64 price) {
    return Message{.type = Message::Type::kUpdate,
                   .checksum = static_cast<u32>(price),
                   .venue_time_ns = price,
                   .capture_time_ns = price + 1,
                   .symbol = "BTC/USD",
                   .buys = {{.price = price, .quantity = 1}},
                   .sells = {}};
  }

  // The records the updates for `prices` leave in a recording, in order.
  static std::vector<std::byte> SerializeUpdates(
      std::ranges::input_range auto&& prices) {
    std::vector<std::byte> bytes;
    for (const i64 price : prices) {
      const Message message = MakeUpdate(price);
      const std::size_t offset = bytes.size();
      bytes.resize(offset + message.GetByteSize());
      message.Serialize(std::span{bytes}.subspan(offset));
    }
    return bytes;
  }

 private:
  std::filesystem::path original_directory_;
  std::filesystem::path directory_;
};

// While recording, the file is named after the pair (without its '/') and
// the start second recorded in its header.
TEST_F(FileWriterTest, OpensTheRecordingUnderItsStartTimeWithoutTheSlash) {
  const FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, std::format("BTCUSD-{}.bin",
                               ReadHeader(ReadBytes(*name)).start_time_ns /
                                   kNanosPerSecond));
}

// A recording opens with exactly the header for its venue, pair, depth,
// precision and start time.
TEST_F(FileWriterTest, StartsTheRecordingWithItsHeader) {
  const i64 before = core::UnixNanosNow();
  {
    const FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
  }
  const i64 after = core::UnixNanosNow();

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  const std::vector<std::byte> bytes = ReadBytes(*name);
  ASSERT_THAT(bytes, SizeIs(sizeof(FileHeader)));

  // The start time is the only field the test cannot know up front.
  const i64 start_time_ns = ReadHeader(bytes).start_time_ns;
  EXPECT_THAT(start_time_ns, AllOf(Ge(before), Le(after)));
  EXPECT_THAT(bytes,
              ElementsAreArray(HeaderBytes(MakeFileHeader(
                  kVenue, "BTC/USD", kDepth, kPrecision, start_time_ns))));
}

// Closing the writer renames the recording to carry its end second too.
TEST_F(FileWriterTest, RenamesTheRecordingWithItsEndTimeOnceClosed) {
  const i64 before_close_s = [] {
    const FileWriter writer{kVenue, "ETH/USD", kDepth, kPrecision};
    return core::UnixNanosNow() / kNanosPerSecond;
  }();
  const i64 after_close_s = core::UnixNanosNow() / kNanosPerSecond;

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  const std::string start_prefix =
      std::format("ETHUSD-{}-",
                  ReadHeader(ReadBytes(*name)).start_time_ns / kNanosPerSecond);
  ASSERT_THAT(*name, StartsWith(start_prefix));

  EXPECT_THAT(std::stoll(name->substr(start_prefix.size())),
              AllOf(Ge(before_close_s), Le(after_close_s)));
}

// Every received message is appended after the header, in order, even
// when the producer outpaces the writer thread.
TEST_F(FileWriterTest, AppendsEveryReceivedMessageInOrder) {
  // Several times the queue capacity, so the writer thread has to keep up.
  constexpr i64 kMessageCount = 3 * FileWriter::kMaxQueuedMessages;
  {
    FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
    for (const i64 price : std::views::iota(i64{0}, kMessageCount)) {
      writer.Receive(MakeUpdate(price));
    }
    EXPECT_FALSE(writer.failed());
  }

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  EXPECT_THAT(Records(ReadBytes(*name)),
              ElementsAreArray(
                  SerializeUpdates(std::views::iota(i64{0}, kMessageCount))));
}

// Each message reaches the file while it is still open, so a killed
// process loses nothing already written.
TEST_F(FileWriterTest, FlushesEachMessageBeforeTheRecordingCloses) {
  FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
  writer.Receive(MakeUpdate(42));

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  // The writer thread may not have run yet; a killed process keeps whatever
  // it has written, so poll rather than close.
  const std::vector<std::byte> expected_records =
      SerializeUpdates(std::array{42});
  std::vector<std::byte> bytes = ReadBytes(*name);
  for (int attempt{0};
       bytes.size() < sizeof(FileHeader) + expected_records.size() &&
       attempt < 1000;
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bytes = ReadBytes(*name);
  }

  EXPECT_THAT(Records(bytes), ElementsAreArray(expected_records));
}

// Writers for different pairs record to separate files, each holding only
// its own pair's messages.
TEST_F(FileWriterTest, KeepsConcurrentRecordingsOfDifferentPairsApart) {
  {
    FileWriter btc{kVenue, "BTC/USD", kDepth, kPrecision};
    FileWriter eth{kVenue, "ETH/USD", kDepth, kPrecision};
    btc.Receive(MakeUpdate(1));
    eth.Receive(MakeUpdate(2));
    eth.Receive(MakeUpdate(3));
  }

  const std::vector<std::string> names = RecordingNames();
  ASSERT_THAT(names, UnorderedElementsAre(StartsWith("BTCUSD-"),
                                          StartsWith("ETHUSD-")));
  // Sorted, so BTC comes first.
  EXPECT_THAT(Records(ReadBytes(names[0])),
              ElementsAreArray(SerializeUpdates(std::array{1})));
  EXPECT_THAT(Records(ReadBytes(names[1])),
              ElementsAreArray(SerializeUpdates(std::array{2, 3})));
}

// Each venue connection gets its own file: the one that ended is closed, and
// the next connection's first message opens another, with its own header.
TEST_F(FileWriterTest, StartsANewFileForEachConnection) {
  {
    FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
    writer.Receive(MakeUpdate(1));
    writer.OnGap(core::Gap::kDisconnected);
    writer.Receive(MakeUpdate(2));
    writer.Receive(MakeUpdate(3));
  }

  const std::vector<std::string> names = RecordingNames();
  ASSERT_THAT(names, SizeIs(2));
  std::vector<std::vector<std::byte>> files =
      names | std::views::transform(&FileWriterTest::ReadBytes) |
      std::ranges::to<std::vector>();
  // Oldest first.
  std::ranges::sort(files, {}, [](const std::vector<std::byte>& bytes) {
    return ReadHeader(bytes).start_time_ns;
  });
  for (const std::vector<std::byte>& bytes : files) {
    EXPECT_EQ(ReadHeader(bytes).depth, kDepth);
  }
  EXPECT_THAT(Records(files[0]),
              ElementsAreArray(SerializeUpdates(std::array{1})));
  EXPECT_THAT(Records(files[1]),
              ElementsAreArray(SerializeUpdates(std::array{2, 3})));
}

// Files that start and end within the same second all keep their messages:
// each final name already taken gets a suffix instead of replacing it.
TEST_F(FileWriterTest, NeverReplacesARecordingOfTheSameSeconds) {
  {
    FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
    for (const i64 price : std::views::iota(i64{0}, 5)) {
      writer.Receive(MakeUpdate(price));
      writer.OnGap(core::Gap::kDisconnected);
    }
  }

  EXPECT_THAT(RecordingNames(), SizeIs(5));
}

// Failed reconnect attempts end no connection with messages on it, so they
// leave no empty files: the file waiting for messages is kept.
TEST_F(FileWriterTest, KeepsAFileWithoutMessagesForTheNextConnection) {
  {
    FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
    writer.OnGap(core::Gap::kDisconnected);
    writer.OnGap(core::Gap::kDisconnected);
    writer.Receive(MakeUpdate(7));
  }

  const std::optional<std::string> name = OnlyRecordingName();
  ASSERT_TRUE(name.has_value());
  EXPECT_THAT(Records(ReadBytes(*name)),
              ElementsAreArray(SerializeUpdates(std::array{7})));
}

// A connection ending last, as at shutdown, opens no file after it.
TEST_F(FileWriterTest, OpensNoFileAfterTheLastConnection) {
  {
    FileWriter writer{kVenue, "BTC/USD", kDepth, kPrecision};
    writer.Receive(MakeUpdate(1));
    writer.OnGap(core::Gap::kDisconnected);
  }

  EXPECT_THAT(RecordingNames(), SizeIs(1));
}

}  // namespace
}  // namespace l2
