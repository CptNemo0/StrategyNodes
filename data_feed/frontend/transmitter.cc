#include "frontend/transmitter.h"

#include <memory>
#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "frontend/outbox.h"
#include "frontend/stream.h"
#include "l2/message.h"
#include "l2/pair_precision.h"
#include "venue/manager.h"

namespace frontend {

Transmitter::Transmitter(Outbox& outbox,
                         venue::SubscriptionId id,
                         std::string_view venue,
                         const std::string& symbol,
                         u32 depth,
                         const l2::PairPrecision& precision)
    : outbox_{outbox},
      id_{id},
      stream_{std::make_shared<Stream>(id, venue, symbol, depth, precision)} {
  outbox_.Register(id_, stream_);
}

Transmitter::~Transmitter() {
  outbox_.Unregister(id_);
}

void Transmitter::Receive(l2::Message message) {
  stream_->Receive(message);
}

void Transmitter::OnGap(core::Gap gap) {
  stream_->Interrupt(gap);
}

}  // namespace frontend
