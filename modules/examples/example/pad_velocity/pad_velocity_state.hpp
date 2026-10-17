#pragma once
// State of the sample user input (modules/examples/example/pad_velocity). Only plain fields and
// domain/types.hpp are included, as the generated input union requires.
#include <cstdint>

#include "../../../../firmware/Orbit12/src/domain/types.hpp"

namespace orbit {
struct PadVelocityInputState {
  static constexpr std::uint8_t kPadCount = 12;
  std::int32_t transpose = 0;                 // parameter: -24..24 semitones
  std::uint8_t velocity = 100;                // parameter: 0..127 scale
  std::uint8_t pad_vel[kPadCount]{127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};  // attribute
  std::uint8_t held_pitch[kPadCount]{};       // pitch sounded by each held pad
  bool held[kPadCount]{};                     // pad has a note-on without a release yet
};
}  // namespace orbit
