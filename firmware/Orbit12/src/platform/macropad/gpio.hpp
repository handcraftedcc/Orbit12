#pragma once
// MacroPad RP2040 key + encoder-switch input and the time source. Owned by
// core 1 (GPIO owner). Arduino includes live in gpio.cpp only.
#include <cstdint>

#include "../../domain/types.hpp"
#include "debounce.hpp"
#include "encoder.hpp"
#include "ports.hpp"

namespace orbit {
namespace platform {

// Wrap-safe time: ms and us derive from one 64-bit microsecond counter.
TimeSample sample_time() noexcept;
ClockPort clock_port() noexcept;

struct GpioSample {
  std::uint16_t key_mask = 0;  // debounced, bit n = key n down
  KeyEvent edges[kKeys]{};      // debounced key edges, ascending key order
  std::uint8_t edge_count = 0;
  bool switch_down = false, switch_pressed = false, switch_released = false;
};

class GpioInput {
 public:
  static constexpr std::uint32_t kKeyScanMs = 3;     // source cadence
  static constexpr std::uint32_t kKeySettleMs = 6;   // two stable scans
  static constexpr std::uint32_t kSwitchScanMs = 1;
  static constexpr std::uint32_t kSwitchSettleMs = 10;  // adafruit_debouncer default

  // Configures pins; keys already held at boot become the initial state
  // without edges. Encoder is polled with use_encoder_interrupt=false.
  void begin(bool use_encoder_interrupt = false) noexcept;
  // Nonblocking, bounded. Fills `out` with edges since the previous call and
  // the current debounced state. Returns true if any edge/encoder motion.
  bool scan(GpioSample& out) noexcept;
  // Encoder delta (detents, sign per source) accumulated since last call.
  std::int16_t take_encoder_delta() noexcept { return encoder_.take_delta(); }
  // InputPort adapter: appends key edges + switch + encoder delta to a frame.
  bool poll_frame(InputFrame& frame) noexcept;
  InputPort input_port() noexcept;
  Encoder& encoder() noexcept { return encoder_; }

 private:
  Debouncer<kKeys> keys_{kKeySettleMs};
  Debouncer<1> switch_{kSwitchSettleMs};
  Encoder encoder_;
  std::uint32_t last_key_ms_ = 0, last_switch_ms_ = 0;
  std::uint16_t key_state_ = 0;
  bool switch_state_ = false;
};

}  // namespace platform
}  // namespace orbit
