#ifndef DATA_FEED_FRONTEND_TRANSMITTER_H_
#define DATA_FEED_FRONTEND_TRANSMITTER_H_

#include <memory>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/outbox.h"
#include "frontend/stream.h"
#include "l2/book_spec.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace frontend {

// The receiver of one live subscription: feeds its messages, and every gap
// in them, into the subscription's Stream, which sends them to every client
// watching.
//
// Registers the Stream with the Outbox under `id` for its whole lifetime, so
// the Server can find it by the id a Watch names. Throws std::logic_error if
// venue::SinkOf(id) is not kLive.
class Transmitter : public core::Receiver<l2::Message> {
 public:
  // `outbox` must outlive the transmitter.
  Transmitter(Outbox& outbox,
              venue::SubscriptionId id,
              std::string_view venue,
              const l2::BookSpec& book,
              const l2::PairPrecision& precision);

  Transmitter(const Transmitter&) = delete;
  Transmitter& operator=(const Transmitter&) = delete;

  // Unregisters, so no client starts watching once it returns.
  ~Transmitter() override;

  // Runs on the parser thread.
  void Receive(l2::Message message) override;

  void OnGap(core::Gap gap) override;

 private:
  Outbox& outbox_;
  const venue::SubscriptionId id_;
  const std::shared_ptr<Stream> stream_;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_TRANSMITTER_H_
