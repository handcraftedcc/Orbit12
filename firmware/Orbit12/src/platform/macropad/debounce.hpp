#pragma once
// Portable (host-compilable) time-based debouncer for up to 16 logical inputs.
// Callers translate active-low pins to "pressed" bits.
#include <cstdint>

#include "../../domain/time.hpp"

namespace orbit {
namespace platform {

template <std::uint8_t N>
class Debouncer {
  static_assert(N > 0 && N <= 16, "Debouncer supports 1..16 inputs");

 public:
  // Each input must hold a new raw level for settle_ms before it is accepted.
  explicit Debouncer(std::uint32_t settle_ms = 5) noexcept : settle_ms_(settle_ms) {}

  // Adopt the initial level without producing an edge (e.g. keys held at boot).
  void reset(std::uint16_t raw, std::uint32_t now_ms) noexcept {
    raw_ = stable_ = static_cast<std::uint16_t>(raw & kMask);
    for (auto& t : changed_at_) t = now_ms;
  }

  // Feeds one raw sample; returns the mask of inputs whose debounced level
  // changed in this call. Wrap-safe in now_ms.
  std::uint16_t update(std::uint16_t raw, std::uint32_t now_ms) noexcept {
    raw = static_cast<std::uint16_t>(raw & kMask);
    std::uint16_t changed = 0;
    for (std::uint8_t i = 0; i < N; ++i) {
      const std::uint16_t bit = static_cast<std::uint16_t>(1U << i);
      if ((raw & bit) != (raw_ & bit)) {
        raw_ = static_cast<std::uint16_t>((raw_ & ~bit) | (raw & bit));
        changed_at_[i] = now_ms;
      } else if ((raw_ & bit) != (stable_ & bit) &&
                 elapsed(now_ms, changed_at_[i]) >= settle_ms_) {
        stable_ = static_cast<std::uint16_t>((stable_ & ~bit) | (raw_ & bit));
        changed = static_cast<std::uint16_t>(changed | bit);
      }
    }
    return changed;
  }

  std::uint16_t stable() const noexcept { return stable_; }

 private:
  static constexpr std::uint16_t kMask =
      static_cast<std::uint16_t>(N == 16 ? 0xFFFFU : ((1U << N) - 1U));
  std::uint32_t settle_ms_;
  std::uint16_t stable_ = 0, raw_ = 0;
  std::uint32_t changed_at_[N]{};
};

}  // namespace platform
}  // namespace orbit
