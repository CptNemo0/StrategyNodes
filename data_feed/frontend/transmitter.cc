#include "frontend/transmitter.h"

#include <string>
#include <string_view>

#include "core/aliasing.h"
#include "l2/message.h"
#include "l2/pair_precision.h"

namespace frontend {

Transmitter::Transmitter(std::string_view /*venue*/,
                         const std::string& /*symbol*/,
                         u32 /*depth*/,
                         const l2::PairPrecision& /*precision*/) {}

void Transmitter::Receive(l2::Message /*message*/) {}

}  // namespace frontend
