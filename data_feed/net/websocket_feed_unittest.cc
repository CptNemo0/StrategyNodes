#include "net/websocket_feed.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline_status.h"
#include "core/unix_time.h"
#include "net/raw_frame.h"

namespace net {
namespace {

using ::testing::IsEmpty;
using ::testing::Message;
using ::testing::Test;

constexpr std::string_view kRawLogStem = "websocket_feed_unittest";

// A numeric host with a service name no system knows, so resolving it fails
// at once without any network access. A refused port would do too, but
// Windows retries those for seconds.
WebsocketFeed::Endpoints UnreachableEndpoint() {
  return {.host = "127.0.0.1",
          .port = "data-feed-unittest-no-such-service",
          .target = "/v2"};
}

// The feed writes its raw logs into the working directory, so each test runs
// in a fresh directory of its own, removed afterwards.
class WebsocketFeedTest : public Test {
 protected:
  void SetUp() override {
    original_directory_ = std::filesystem::current_path();
    directory_ = std::filesystem::temp_directory_path() /
                 std::format("data_feed_websocket_feed_unittest-{}",
                             core::UnixNanosNow());
    std::filesystem::create_directories(directory_);
    std::filesystem::current_path(directory_);
  }

  void TearDown() override {
    feed_.reset();
    std::filesystem::current_path(original_directory_);
    std::filesystem::remove_all(directory_);
  }

  WebsocketFeed& feed() {
    if (!feed_) {
      feed_ = std::make_unique<WebsocketFeed>(
          UnreachableEndpoint(), std::string{kRawLogStem}, *frames_, status_);
    }
    return *feed_;
  }

  static std::vector<std::string> RawLogNames() {
    return std::filesystem::directory_iterator{"."} |
           std::views::transform([](const std::filesystem::directory_entry& e) {
             return e.path().filename().string();
           }) |
           std::views::filter([](const std::string& name) {
             return name.starts_with(kRawLogStem) && name.ends_with(".txt");
           }) |
           std::ranges::to<std::vector>();
  }

  // Heap allocated: it holds its 256 frames inline.
  std::unique_ptr<RawFrameQueue> frames_ = std::make_unique<RawFrameQueue>();
  core::PipelineStatus status_;

 private:
  std::filesystem::path original_directory_;
  std::filesystem::path directory_;
  std::unique_ptr<WebsocketFeed> feed_;
};

// Sending while disconnected is dropped without side effects.
TEST_F(WebsocketFeedTest, IgnoresSendingWhileDisconnected) {
  feed().Send(R"({"method":"ping"})");

  EXPECT_EQ(frames_->read_available(), 0u);
  EXPECT_FALSE(status_.failed);
}

// Disconnecting a feed that never connected is harmless and opens no raw
// log.
TEST_F(WebsocketFeedTest, DisconnectingWhileDisconnectedDoesNothing) {
  feed().Disconnect();
  feed().Disconnect();

  EXPECT_FALSE(status_.failed);
  EXPECT_THAT(RawLogNames(), IsEmpty());
}

// A connection that cannot be made throws to the caller.
TEST_F(WebsocketFeedTest, ThrowsWhenTheVenueIsUnreachable) {
  EXPECT_ANY_THROW(feed().Connect());
}

// A failed connect leaves nothing running and does not mark the pipeline
// failed, as no thread died.
TEST_F(WebsocketFeedTest, StaysDisconnectedAndHealthyAfterAFailedConnect) {
  ASSERT_ANY_THROW(feed().Connect());

  // A connect that throws leaves nothing running: sending is ignored, and the
  // pipeline is not marked failed, as no thread died.
  feed().Send(R"({"method":"ping"})");
  feed().Disconnect();
  EXPECT_EQ(frames_->read_available(), 0u);
  EXPECT_FALSE(status_.failed);
}

// Connect() may be called again after it failed.
TEST_F(WebsocketFeedTest, CanRetryAFailedConnect) {
  for (int attempt{1}; attempt <= 2; ++attempt) {
    SCOPED_TRACE(Message() << "attempt " << attempt);
    EXPECT_ANY_THROW(feed().Connect());
  }
  EXPECT_FALSE(status_.failed);
}

}  // namespace
}  // namespace net
