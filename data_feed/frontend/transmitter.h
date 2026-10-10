#ifndef DATA_FEED_FRONTEND_TRANSMITTER_H_
#define DATA_FEED_FRONTEND_TRANSMITTER_H_

#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "core/receiver.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace frontend {

// Local websocket endpoint the TypeScript frontend subscribes to; must match
// the default VITE_FEED_URL in frontend/src/App.tsx.
inline constexpr u16 kStreamPort = 8765;

// Streams the l2::Messages of a single pair to the frontend. Not implemented
// yet: every message is dropped.
class Transmitter : public core::Receiver<l2::Message> {
 public:
  // Same arguments as l2::FileWriter, so either can back a subscription.
  Transmitter(std::string_view venue,
              const std::string& symbol,
              u32 depth,
              const l2::PairPrecision& precision);

  Transmitter(const Transmitter&) = delete;
  Transmitter& operator=(const Transmitter&) = delete;

  ~Transmitter() override = default;

  void Receive(l2::Message message) override;
};

}  // namespace frontend

#endif  // ! DATA_FEED_FRONTEND_TRANSMITTER_H_
