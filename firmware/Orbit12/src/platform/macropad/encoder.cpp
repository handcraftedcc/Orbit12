#include "platform/macropad/encoder.hpp"

#include <Arduino.h>

namespace orbit {
namespace platform {
namespace {
Encoder* g_irq_encoder = nullptr;
void encoder_isr() { if (g_irq_encoder) g_irq_encoder->service(); }
}  // namespace

void Encoder::begin(bool use_interrupt) noexcept {
  pinMode(PIN_ROTA, INPUT_PULLUP);
  pinMode(PIN_ROTB, INPUT_PULLUP);
  decoder_.reset(static_cast<std::uint8_t>((digitalRead(PIN_ROTA) << 1) | digitalRead(PIN_ROTB)));
  irq_ = use_interrupt;
  if (use_interrupt) {
    g_irq_encoder = this;
    attachInterrupt(digitalPinToInterrupt(PIN_ROTA), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ROTB), encoder_isr, CHANGE);
  }
}

void Encoder::service() noexcept {
  const std::uint8_t state =
      static_cast<std::uint8_t>((digitalRead(PIN_ROTA) << 1) | digitalRead(PIN_ROTB));
  const std::int8_t step = decoder_.feed(state);
  if (step != 0) mailbox_.add(step);
}

}  // namespace platform
}  // namespace orbit
