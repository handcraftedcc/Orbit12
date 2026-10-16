#pragma once
// Example user processor: raises every note by one octave. Stateless (see module.json).
// Relative to this file (modules/examples/example/octave_shift). Inside firmware/Orbit12/src/modules/user/<author>/<name>/
// the path is "../../../common/module_state.hpp" instead.
#include "../../../../firmware/Orbit12/src/modules/common/module_state.hpp"

namespace orbit {
const ModuleDescriptor& user_example_octave_shift_module_descriptor() noexcept;
}  // namespace orbit
