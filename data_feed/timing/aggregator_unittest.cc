#include "timing/aggregator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "core/aliasing.h"
#include "core/unix_time.h"

namespace timing {
namespace {

using ::testing::ElementsAre;
using ::testing::Test;
using ::testing::UnorderedElementsAre;

std::vector<std::string> ReadLines(const std::filesystem::path& path) {
  std::ifstream input{path};
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) {
    lines.push_back(line);
  }
  return lines;
}

// Each test writes its own stats file, removed afterwards.
class AggregatorTest : public Test {
 protected:
  void TearDown() override {
    Aggregator::Stop();
    std::filesystem::remove(path_);
  }

  const std::filesystem::path path_ =
      std::filesystem::temp_directory_path() /
      std::format("data_feed_timing_unittest-{}.txt", core::UnixNanosNow());
};

// Samples from several threads all land in the file, by name, and the file
// closes with the drop count.
TEST_F(AggregatorTest, WritesSamplesFromEveryThread) {
  Aggregator::Start(path_);
  std::jthread{[] {
    Aggregator::Record(Interval::kExchangeToParseStart, 10);
  }}.join();
  std::jthread{[] {
    Aggregator::Record(Interval::kReceiveToParseComplete, 20);
  }}.join();
  Aggregator::Record(Interval::kExchangeToParseStart, 30);
  Aggregator::Stop();

  const std::vector<std::string> lines = ReadLines(path_);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.back(), "# dropped 0");
  EXPECT_THAT(std::vector(lines.begin(), lines.end() - 1),
              UnorderedElementsAre("exchange_to_parse_start 10",
                                   "receive_to_parse_complete 20",
                                   "exchange_to_parse_start 30"));
}

// One thread's samples keep the order they were recorded in.
TEST_F(AggregatorTest, KeepsOrderWithinThread) {
  Aggregator::Start(path_);
  for (const i64 nanos : {3, 1, 2}) {
    Aggregator::Record(Interval::kExchangeToParseStart, nanos);
  }
  Aggregator::Stop();

  EXPECT_THAT(
      ReadLines(path_),
      ElementsAre("exchange_to_parse_start 3", "exchange_to_parse_start 1",
                  "exchange_to_parse_start 2", "# dropped 0"));
}

// Before Start(), Record() is a no-op.
TEST_F(AggregatorTest, IgnoresSamplesWhileStopped) {
  Aggregator::Record(Interval::kExchangeToParseStart, 99);
  Aggregator::Start(path_);
  Aggregator::Stop();

  EXPECT_THAT(ReadLines(path_), ElementsAre("# dropped 0"));
}

}  // namespace
}  // namespace timing
