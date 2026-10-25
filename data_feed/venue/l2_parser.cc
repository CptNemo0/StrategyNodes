#include "venue/l2_parser.h"

#include <cstdio>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "core/aliasing.h"
#include "core/debug_log.h"
#include "core/pipeline_status.h"
#include "core/receiver.h"
#include "core/spsc.h"
#include "core/unix_time.h"
#include "l2/book.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "net/raw_frame.h"
#include "timing/aggregator.h"
#include "venue/l2_decoder.h"

namespace venue {

L2Parser::L2Parser(net::RawFrameQueue& frames,
                   core::PipelineStatus& status,
                   std::optional<ChecksumCheck> checksum_check)
    : frames_{frames},
      status_{status},
      checksum_check_{std::move(checksum_check)} {}

L2Parser::~L2Parser() {
  Stop();
}

void L2Parser::Start(std::unique_ptr<L2Decoder> decoder) {
  {
    // A new connection starts every pair over with a snapshot; anything
    // before it would be left over from the last one.
    const std::scoped_lock lock{routes_mutex_};
    // flat_map hands out its values only through its iterators' proxies.
    for (auto&& [symbol, route] : routes_) {
      route.awaiting_snapshot = true;
    }
  }
  // The thread is stopped, so nothing uses the last connection's decoder.
  decoder_ = std::move(decoder);
  thread_ = std::jthread{[this](const std::stop_token& stop) {
    core::Drain(
        stop, "parser", frames_,
        [this](const net::RawFrame& frame) { Dispatch(frame); },
        [this] { status_.Fail(); });
  }};
}

void L2Parser::Stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void L2Parser::Attach(const std::string& symbol,
                      const l2::PairPrecision& precision,
                      u32 depth,
                      core::Receiver<l2::Message>& receiver) {
  const std::scoped_lock lock{routes_mutex_};
  if (!routes_
           .try_emplace(symbol, Route{.precision = precision,
                                      .receiver = &receiver,
                                      .book = l2::Book{depth}})
           .second) {
    throw std::invalid_argument{
        std::format("{} already has a receiver", symbol)};
  }
}

void L2Parser::Detach(std::string_view symbol) {
  const std::scoped_lock lock{routes_mutex_};
  const auto route = routes_.find(symbol);
  if (route != routes_.end()) {
    routes_.erase(route);
  }
}

void L2Parser::Dispatch(const net::RawFrame& frame) {
  const std::scoped_lock lock{routes_mutex_};
  parse_start_ns_ = core::UnixNanosNow();
  decoder_->Decode(frame, *this);
  timing::Aggregator::Record(timing::Interval::kReceiveToParseComplete,
                             core::UnixNanosNow() - parse_start_ns_);
}

const l2::PairPrecision* L2Parser::Find(std::string_view symbol,
                                        l2::Message::Type type) const {
  const auto route = FindRoute(symbol, type);
  return route == routes_.end() ? nullptr : &route->second.precision;
}

void L2Parser::Emit(l2::Message message) {
  const auto route = FindRoute(message.symbol, message.type);
  if (route == routes_.end()) {
    return;
  }
  Route& routed = route->second;
  // Only updates: a snapshot's venue time may be that of an older change.
  // Zero means the venue sent none.
  if (message.type == l2::Message::Type::kUpdate &&
      message.venue_time_ns != 0) {
    timing::Aggregator::Record(timing::Interval::kExchangeToParseStart,
                               parse_start_ns_ - message.venue_time_ns);
  }
  if (std::exchange(routed.awaiting_snapshot, false)) {
    DEBUG_LOG("{}: snapshot received, streaming", route->first);
  }

  if (checksum_check_) {
    routed.book.Apply(message);
    // Zero means the venue sent none, so there is nothing to check.
    if (const u32 expected = message.checksum; expected != 0) {
      if (const u32 actual = checksum_check_->checksum(routed.book);
          actual != expected) {
        std::println(stderr,
                     "{}: book checksum {} does not match the venue's {}; "
                     "resubscribing",
                     route->first, actual, expected);
        routed.awaiting_snapshot = true;
        routed.book.Clear();
        routed.receiver->OnGap(core::Gap::kResubscribing);
        checksum_check_->on_mismatch(route->first);
        return;
      }
    }
  }
  routed.receiver->Receive(std::move(message));
}

}  // namespace venue
