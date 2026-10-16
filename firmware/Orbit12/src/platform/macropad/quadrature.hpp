#pragma once
// Portable quadrature decoding plus a load/store-only delta mailbox.
// Cortex-M0+ has no lock-free read-modify-write atomics, so the delta is
// published as a monotonically increasing producer total (single writer) and
// consumed by differencing against a consumer-private total (single reader).
#include <atomic>
#include <cstdint>

namespace orbit {
namespace platform {

// Source: rotaryio.IncrementalEncoder (4 quarter-steps per detent) and the
// Python wrapper returns position * -1. The source-derived sign was -1; on the
// flashed MacroPad the knob turned the wrong way, so the polarity is set to +1
// here, and this is the ONLY place the encoder sign is applied (the adapter and
// the UI layer must not negate again). Clockwise must produce +1 (value up /
// selection next).
inline constexpr std::int8_t kMacroPadEncoderPolarityHardwareVerified = +1;

class QuadratureDecoder {
 public:
  // Hardware-verified polarity: inverted relative to the initial source-derived
  // assumption (-1). Change only with device verification.
  static constexpr std::int8_t kInvert = kMacroPadEncoderPolarityHardwareVerified;
  static constexpr std::int8_t kStepsPerDetent = 4;

  void reset(std::uint8_t state) noexcept { prev_ = static_cast<std::uint8_t>(state & 3U); sub_ = 0; }

  // `state` is (A << 1) | B. Returns detents completed by this sample
  // (already sign-corrected), usually 0 or +/-1. A transition that flips both
  // bits is a glitch/missed edge: counted, ignored, and the state resyncs.
  std::int8_t feed(std::uint8_t state) noexcept {
    state = static_cast<std::uint8_t>(state & 3U);
    if (state == prev_) return 0;
    static constexpr std::int8_t kTable[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                               -1, 0, 0, 1, 0, 1, -1, 0};
    const std::int8_t step = kTable[(prev_ << 2) | state];
    prev_ = state;
    if (step == 0) { ++glitches_; return 0; }
    sub_ = static_cast<std::int8_t>(sub_ + step);
    if (sub_ >= kStepsPerDetent) { sub_ = 0; return static_cast<std::int8_t>(kInvert); }
    if (sub_ <= -kStepsPerDetent) { sub_ = 0; return static_cast<std::int8_t>(-kInvert); }
    return 0;
  }

  std::uint32_t glitches() const noexcept { return glitches_; }

 private:
  std::uint8_t prev_ = 0;
  std::int8_t sub_ = 0;
  std::uint32_t glitches_ = 0;
};

class DeltaMailbox {
  static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t), "compact atomic");

 public:
  // Producer side only (ISR or poller, one context).
  void add(std::int8_t detents) noexcept {
    produced_.store(produced_.load(std::memory_order_relaxed) +
                        static_cast<std::uint32_t>(static_cast<std::int32_t>(detents)),
                    std::memory_order_release);
  }
  // Consumer side only; returns the delta since the previous take, saturated
  // to int16 (the remainder stays queued for the next call).
  std::int16_t take() noexcept {
    const std::uint32_t total = produced_.load(std::memory_order_acquire);
    std::int32_t pending = static_cast<std::int32_t>(total - consumed_);
    if (pending > 32767) pending = 32767;
    if (pending < -32768) pending = -32768;
    consumed_ += static_cast<std::uint32_t>(pending);
    return static_cast<std::int16_t>(pending);
  }

 private:
  std::atomic<std::uint32_t> produced_{0};
  std::uint32_t consumed_ = 0;
};

}  // namespace platform
}  // namespace orbit
