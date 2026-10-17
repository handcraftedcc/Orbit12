#pragma once
// Example user input: pads 0..11 play MIDI 60..71 (plus transpose) with a per-pad velocity.
// Relative to this file (modules/examples/example/pad_velocity). Inside firmware/Orbit12/src/modules/user/<author>/<name>/
// the path is "../../../common/input_state.hpp" instead.
#include "../../../../firmware/Orbit12/src/modules/common/input_state.hpp"

namespace orbit {
const InputDescriptor& user_example_pad_velocity_input_descriptor() noexcept;
}  // namespace orbit
