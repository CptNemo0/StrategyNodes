#include "frontend/server.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <algorithm>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <expected>
#include <flat_map>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/outbox.h"
#include "frontend/wire.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"
#include "venue/router.h"

namespace frontend {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Test;

constexpr l2::PairPrecision kPrecision{.price_decimals = 1, .qty_decimals = 8};
// How long the client waits for a frame before the test fails.
constexpr u32 kReadTimeoutMs = 5000;

// A venue that is always healthy and builds each live subscription's receiver
// at once, so a test can feed it messages. Recordings get no receiver, which
// spares the tests a .bin file.
class FakeManager : public venue::Manager {
 public:
  std::string_view name() const override { return "fake"; }

  void Connect() override {}

  bool healthy() const override { return true; }

  std::expected<venue::SubscriptionId, venue::Error> Subscribe(
      venue::SubscriptionId id,
      const std::string& symbol,
      u32 depth,
      venue::Sink sink,
      venue::ReceiverFactory make_receiver) override {
    const std::scoped_lock lock{mutex_};
    if (depth == 0) {
      return std::unexpected{venue::Error{
          .code = venue::Error::Code::kInvalidDepth, .detail = "no depth 0"}};
    }
    subscriptions_.emplace(
        id, Subscription{.info = venue::SubscriptionInfo{.id = id,
                                                         .symbol = symbol,
                                                         .depth = depth,
                                                         .sink = sink,
                                                         .start_time_ns = 42},
                         .receiver = sink == venue::Sink::kLive
                                         ? make_receiver(kPrecision)
                                         : nullptr});
    return id;
  }

  std::expected<void, venue::Error> Unsubscribe(venue::SubscriptionId id,
                                                venue::Sink sink) override {
    const std::scoped_lock lock{mutex_};
    const auto subscription = subscriptions_.find(id);
    if (subscription == subscriptions_.end()) {
      return std::unexpected{venue::Error{
          .code = venue::Error::Code::kUnknownId, .detail = "not here"}};
    }
    if (subscription->second.info.sink != sink) {
      return std::unexpected{venue::Error{
          .code = venue::Error::Code::kSinkMismatch, .detail = "other sink"}};
    }
    subscriptions_.erase(subscription);
    return {};
  }

  std::vector<venue::SubscriptionInfo> List() const override {
    const std::scoped_lock lock{mutex_};
    std::vector<venue::SubscriptionInfo> infos;
    for (const Subscription& subscription : subscriptions_.values()) {
      infos.push_back(subscription.info);
    }
    return infos;
  }

  std::expected<venue::PairList, venue::Error> ListPairs() override {
    const std::scoped_lock lock{mutex_};
    return pairs_;
  }

  // What ListPairs() returns from then on.
  void AnswerListPairs(std::expected<venue::PairList, venue::Error> pairs) {
    const std::scoped_lock lock{mutex_};
    pairs_ = std::move(pairs);
  }

  // Hands `message` to live subscription `id`'s receiver, as the parser would.
  void Feed(venue::SubscriptionId id, l2::Message message) {
    const std::scoped_lock lock{mutex_};
    subscriptions_.at(id).receiver->Receive(std::move(message));
  }

  // Tells live subscription `id`'s receiver its connection ended.
  void Disconnect(venue::SubscriptionId id) {
    const std::scoped_lock lock{mutex_};
    subscriptions_.at(id).receiver->OnGap(core::Gap::kDisconnected);
  }

 private:
  struct Subscription {
    venue::SubscriptionInfo info;
    std::unique_ptr<core::Receiver<l2::Message>> receiver;
  };

  mutable std::mutex mutex_;
  std::flat_map<venue::SubscriptionId, Subscription> subscriptions_;
  std::expected<venue::PairList, venue::Error> pairs_{
      venue::PairList{.symbols = {"BTC/USD", "ETH/USD"}, .depths = {10, 100}}};
};

// A blocking websocket client speaking the wire protocol.
class Client {
 public:
  explicit Client(u16 port) : ws_{ioc_} {
    boost::asio::ip::tcp::resolver resolver{ioc_};
    boost::asio::connect(ws_.next_layer(),
                         resolver.resolve("127.0.0.1", std::to_string(port)));
#ifdef _WIN32
    // Fails a read that would otherwise hang the test forever.
    const DWORD timeout = kReadTimeoutMs;
    setsockopt(ws_.next_layer().native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#endif
    ws_.handshake("127.0.0.1", "/");
    ws_.binary(true);
  }

  void SendRaw(std::string_view bytes) {
    ws_.write(boost::asio::buffer(bytes.data(), bytes.size()));
  }

  void Send(const wire::ClientCommand& command) {
    SendRaw(command.SerializeAsString());
  }

  wire::MarketDataEvent Read() {
    boost::beast::flat_buffer buffer;
    ws_.read(buffer);
    wire::MarketDataEvent event;
    EXPECT_TRUE(
        event.ParseFromString(boost::beast::buffers_to_string(buffer.data())));
    events_.push_back(event);
    return event;
  }

  // Reads until an event matches `predicate`, and returns it.
  wire::MarketDataEvent ReadUntil(
      const std::function<bool(const wire::MarketDataEvent&)>& predicate) {
    while (true) {
      if (wire::MarketDataEvent event = Read(); predicate(event)) {
        return event;
      }
    }
  }

  // Sends `command` under the next request id and returns its result,
  // keeping every other event read meanwhile in events().
  wire::CommandResult Command(wire::ClientCommand command) {
    command.set_request_id(++last_request_id_);
    Send(command);
    return ReadUntil([this](const wire::MarketDataEvent& event) {
             return event.has_result() &&
                    event.result().request_id() == last_request_id_;
           })
        .result();
  }

  const std::vector<wire::MarketDataEvent>& events() const { return events_; }

  void Close() { ws_.close(boost::beast::websocket::close_code::normal); }

 private:
  boost::asio::io_context ioc_;
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;
  u64 last_request_id_{0};
  std::vector<wire::MarketDataEvent> events_;
};

wire::ClientCommand StartCommand(std::string symbol,
                                 u32 depth,
                                 wire::Sink sink) {
  wire::ClientCommand command;
  wire::Start& start = *command.mutable_start();
  start.set_venue(wire::VENUE_KRAKEN);
  start.set_symbol(std::move(symbol));
  start.set_depth(depth);
  start.set_sink(sink);
  return command;
}

wire::ClientCommand StopCommand(u64 id, wire::Sink sink) {
  wire::ClientCommand command;
  command.mutable_stop()->set_id(id);
  command.mutable_stop()->set_sink(sink);
  return command;
}

wire::ClientCommand ListCommand() {
  wire::ClientCommand command;
  command.mutable_list();
  return command;
}

wire::ClientCommand ListPairsCommand(wire::Venue venue) {
  wire::ClientCommand command;
  command.mutable_list_pairs()->set_venue(venue);
  return command;
}

wire::ClientCommand WatchCommand(u64 id) {
  wire::ClientCommand command;
  command.mutable_watch()->set_id(id);
  return command;
}

wire::ClientCommand UnwatchCommand() {
  wire::ClientCommand command;
  command.mutable_unwatch();
  return command;
}

l2::Message Snapshot(i64 bid_price) {
  return l2::Message{.type = l2::Message::Type::kSnapshot,
                     .checksum = 9,
                     .venue_time_ns = 1,
                     .capture_time_ns = 2,
                     .symbol = "BTC/USD",
                     .buys = {{.price = bid_price, .quantity = 3}},
                     .sells = {}};
}

class ServerTest : public Test {
 protected:
  ServerTest() {
    venue::Router::Managers managers;
    managers.emplace(venue::Id::kKraken, std::move(owned_fake_));
    router_.emplace(std::move(managers), outbox_);
    server_.emplace(*router_, outbox_, ServerAddress{.port = 0});
  }

  // Connects a new client.
  Client Connect() const { return Client{server_->port()}; }

  // Declared in the order main uses: the server goes first, the outbox last.
  Outbox outbox_;
  std::unique_ptr<FakeManager> owned_fake_ = std::make_unique<FakeManager>();
  FakeManager& fake_ = *owned_fake_;
  std::optional<venue::Router> router_;
  std::optional<Server> server_;
};

TEST_F(ServerTest, ListsNothingAtFirst) {
  Client client = Connect();

  const wire::CommandResult result = client.Command(ListCommand());

  ASSERT_TRUE(result.has_subscriptions());
  EXPECT_THAT(result.subscriptions().subscriptions(), IsEmpty());
  EXPECT_EQ(result.request_id(), 1u);
}

TEST_F(ServerTest, StartsAndListsSubscriptions) {
  Client client = Connect();

  EXPECT_EQ(
      client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE)).started_id(),
      1u);
  EXPECT_EQ(client.Command(StartCommand("ETH/USD", 100, wire::SINK_RECORD))
                .started_id(),
            2u);

  const wire::CommandResult result = client.Command(ListCommand());
  ASSERT_THAT(result.subscriptions().subscriptions(), SizeIs(2));
  const wire::Subscription& live = result.subscriptions().subscriptions(0);
  EXPECT_EQ(live.id(), 1u);
  EXPECT_EQ(live.venue(), wire::VENUE_KRAKEN);
  EXPECT_EQ(live.symbol(), "BTC/USD");
  EXPECT_EQ(live.depth(), 10u);
  EXPECT_EQ(live.sink(), wire::SINK_LIVE);
  EXPECT_EQ(live.start_time_ns(), 42);
  EXPECT_EQ(result.subscriptions().subscriptions(1).sink(), wire::SINK_RECORD);
}

TEST_F(ServerTest, PassesOnTheVenuesError) {
  Client client = Connect();

  const wire::CommandResult result =
      client.Command(StartCommand("BTC/USD", 0, wire::SINK_LIVE));

  EXPECT_EQ(result.error().code(), wire::CommandError::CODE_INVALID_DEPTH);
  EXPECT_EQ(result.error().detail(), "no depth 0");
}

TEST_F(ServerTest, ListsTheVenuesPairs) {
  Client client = Connect();

  const wire::CommandResult result =
      client.Command(ListPairsCommand(wire::VENUE_KRAKEN));

  ASSERT_TRUE(result.has_pairs());
  ASSERT_THAT(result.pairs().pairs(), SizeIs(2));
  EXPECT_EQ(result.pairs().pairs(0).symbol(), "BTC/USD");
  EXPECT_EQ(result.pairs().pairs(1).symbol(), "ETH/USD");
  EXPECT_THAT(result.pairs().depths(), ElementsAre(10u, 100u));
}

TEST_F(ServerTest, PassesOnTheVenuesErrorListingPairs) {
  fake_.AnswerListPairs(std::unexpected{venue::Error{
      .code = venue::Error::Code::kVenueFailed, .detail = "unreachable"}});
  Client client = Connect();

  const wire::CommandResult result =
      client.Command(ListPairsCommand(wire::VENUE_KRAKEN));

  EXPECT_EQ(result.error().code(), wire::CommandError::CODE_VENUE_FAILED);
  EXPECT_EQ(result.error().detail(), "unreachable");
}

TEST_F(ServerTest, RefusesToListPairsWithoutAVenue) {
  Client client = Connect();

  EXPECT_EQ(
      client.Command(ListPairsCommand(wire::VENUE_UNSPECIFIED)).error().code(),
      wire::CommandError::CODE_INVALID_COMMAND);
}

TEST_F(ServerTest, RefusesAStartWithoutASink) {
  Client client = Connect();

  EXPECT_EQ(client.Command(StartCommand("BTC/USD", 10, wire::SINK_UNSPECIFIED))
                .error()
                .code(),
            wire::CommandError::CODE_INVALID_COMMAND);
}

TEST_F(ServerTest, RefusesACommandWithNothingInIt) {
  Client client = Connect();

  EXPECT_EQ(client.Command(wire::ClientCommand{}).error().code(),
            wire::CommandError::CODE_INVALID_COMMAND);
}

TEST_F(ServerTest, AnswersAFrameThatIsNotACommand) {
  Client client = Connect();

  client.SendRaw("\xff\xff\xff");

  EXPECT_EQ(client.Read().result().error().code(),
            wire::CommandError::CODE_INVALID_COMMAND);
  // The connection survives it.
  EXPECT_TRUE(client.Command(ListCommand()).has_subscriptions());
}

TEST_F(ServerTest, StopsOnlyASubscriptionOfTheSinkNamed) {
  Client client = Connect();
  ASSERT_EQ(
      client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE)).started_id(),
      1u);

  EXPECT_EQ(client.Command(StopCommand(1, wire::SINK_RECORD)).error().code(),
            wire::CommandError::CODE_SINK_MISMATCH);
  EXPECT_TRUE(client.Command(StopCommand(1, wire::SINK_LIVE)).has_ok());
  EXPECT_EQ(client.Command(StopCommand(1, wire::SINK_LIVE)).error().code(),
            wire::CommandError::CODE_UNKNOWN_ID);
}

TEST_F(ServerTest, StreamsAWatchedSubscription) {
  Client client = Connect();
  ASSERT_EQ(
      client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE)).started_id(),
      1u);

  ASSERT_TRUE(client.Command(WatchCommand(1)).has_ok());
  fake_.Feed(1, Snapshot(100));
  const wire::MarketDataEvent book = client.ReadUntil(
      [](const wire::MarketDataEvent& event) { return event.has_book(); });

  EXPECT_EQ(book.book().stream_id(), 1u);
  EXPECT_EQ(book.book().kind(), wire::Book::KIND_SNAPSHOT);
  ASSERT_THAT(book.book().bids(), SizeIs(1));
  EXPECT_EQ(book.book().bids(0).price(), 100);
  EXPECT_EQ(book.book().checksum(), 9u);
  // The StreamInfo came first.
  const auto info = std::ranges::find_if(
      client.events(), [](const wire::MarketDataEvent& event) {
        return event.has_stream_info();
      });
  ASSERT_NE(info, client.events().end());
  EXPECT_EQ(info->stream_info().price_decimals(), 1u);
}

TEST_F(ServerTest, NumbersEveryFrameOfAConnection) {
  Client client = Connect();
  ASSERT_TRUE(client.Command(ListCommand()).has_subscriptions());
  ASSERT_TRUE(client.Command(ListCommand()).has_subscriptions());

  ASSERT_THAT(client.events(), SizeIs(2));
  EXPECT_EQ(client.events()[0].sequence(), 0u);
  EXPECT_EQ(client.events()[1].sequence(), 1u);
  EXPECT_GT(client.events()[1].send_time_ns(), 0);
}

TEST_F(ServerTest, StopsStreamingOnceUnwatched) {
  Client client = Connect();
  ASSERT_TRUE(client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE))
                  .started_id());
  ASSERT_TRUE(client.Command(WatchCommand(1)).has_ok());

  ASSERT_TRUE(client.Command(UnwatchCommand()).has_ok());
  fake_.Feed(1, Snapshot(100));

  // Anything sent before the List's answer arrives before it.
  ASSERT_TRUE(client.Command(ListCommand()).has_subscriptions());
  EXPECT_FALSE(std::ranges::any_of(
      client.events(),
      [](const wire::MarketDataEvent& event) { return event.has_book(); }));
}

TEST_F(ServerTest, TellsAWatcherWhenTheVenueConnectionEnds) {
  Client client = Connect();
  ASSERT_TRUE(client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE))
                  .started_id());
  ASSERT_TRUE(client.Command(WatchCommand(1)).has_ok());

  fake_.Disconnect(1);

  EXPECT_EQ(client
                .ReadUntil([](const wire::MarketDataEvent& event) {
                  return event.has_feed_status();
                })
                .feed_status()
                .state(),
            wire::FeedStatus::STATE_DISCONNECTED);
}

TEST_F(ServerTest, RefusesToWatchAnUnknownId) {
  Client client = Connect();

  EXPECT_EQ(client.Command(WatchCommand(5)).error().code(),
            wire::CommandError::CODE_UNKNOWN_ID);
}

TEST_F(ServerTest, RefusesToWatchARecording) {
  Client client = Connect();
  ASSERT_EQ(client.Command(StartCommand("BTC/USD", 10, wire::SINK_RECORD))
                .started_id(),
            1u);

  EXPECT_EQ(client.Command(WatchCommand(1)).error().code(),
            wire::CommandError::CODE_SINK_MISMATCH);
}

TEST_F(ServerTest, KeepsStreamingToOtherClientsWhenOneLeaves) {
  Client leaving = Connect();
  Client staying = Connect();
  ASSERT_TRUE(leaving.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE))
                  .started_id());
  ASSERT_TRUE(leaving.Command(WatchCommand(1)).has_ok());
  ASSERT_TRUE(staying.Command(WatchCommand(1)).has_ok());

  leaving.Close();
  fake_.Feed(1, Snapshot(100));

  EXPECT_EQ(staying
                .ReadUntil([](const wire::MarketDataEvent& event) {
                  return event.has_book();
                })
                .book()
                .bids(0)
                .price(),
            100);
}

TEST_F(ServerTest, ShutsDownWithAClientStillWatching) {
  Client client = Connect();
  ASSERT_TRUE(client.Command(StartCommand("BTC/USD", 10, wire::SINK_LIVE))
                  .started_id());
  ASSERT_TRUE(client.Command(WatchCommand(1)).has_ok());

  server_.reset();

  // The transmitter outlives the server, and must not reach its sessions.
  fake_.Feed(1, Snapshot(100));
}

}  // namespace
}  // namespace frontend
