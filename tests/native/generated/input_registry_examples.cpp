// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Source: module.json manifests (registry order; user inputs after the core inputs).
#include <cstdint>
#include "../../../firmware/Orbit12/src/modules/common/input_registry.hpp"
#include "module_counts.hpp"
#include "../../../firmware/Orbit12/src/modules/common/input_state.hpp"
#include "../../../firmware/Orbit12/src/modules/core/note_walk/walk_inputs.hpp"
#include "../../../modules/examples/example/pad_velocity/pad_velocity.hpp"

namespace orbit {
const InputDescriptor& note_input_descriptor() noexcept;
const InputDescriptor& drum_input_descriptor() noexcept;
const InputDescriptor& chords_input_descriptor() noexcept;
const InputDescriptor& matrix_input_descriptor() noexcept;
const InputDescriptor& note_walk_input_descriptor() noexcept;
const InputDescriptor& chord_walk_input_descriptor() noexcept;
const InputDescriptor& user_example_pad_velocity_input_descriptor() noexcept;
const InputDescriptor* input_module_at(std::uint8_t index) noexcept {
  switch (index) {
    case 0: return &note_input_descriptor();
    case 1: return &drum_input_descriptor();
    case 2: return &chords_input_descriptor();
    case 3: return &matrix_input_descriptor();
    case 4: return &note_walk_input_descriptor();
    case 5: return &chord_walk_input_descriptor();
    case 6: return &user_example_pad_velocity_input_descriptor();
    default: return nullptr;
  }
}
std::uint8_t input_module_count() noexcept { return kGeneratedInputCount; }
}  // namespace orbit
