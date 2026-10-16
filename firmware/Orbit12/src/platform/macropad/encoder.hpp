#pragma once
// Rotary encoder adapter. Owned by core 1 (peripheral owner): service() is
// called from the core-1 loop or from the GPIO edge interrupt registered on
// core 1; take_delta() is called from the same core. Arduino includes: .cpp only.
#include <cstdint>

#include "platform/macropad/quadrature.hpp"

namespace orbit {
namespace platform {

class Encoder {
 public:
  // use_interrupt registers CHANGE interrupts on ROTA/ROTB. Default is
  // polling because interrupt routing on core 1 is not yet device-verified.
  void begin(bool use_interrupt = false) noexcept;
  void service() noexcept;               // read pins, decode, publish; no blocking
  void poll() noexcept { if (!irq_) service(); }  // loop-side call; no-op when ISR-driven
  std::int16_t take_delta() noexcept { return mailbox_.take(); }
  std::uint32_t glitches() const noexcept { return decoder_.glitches(); }

 private:
  QuadratureDecoder decoder_;
  DeltaMailbox mailbox_;
  bool irq_ = false;
};

}  // namespace platform
}  // namespace orbit
