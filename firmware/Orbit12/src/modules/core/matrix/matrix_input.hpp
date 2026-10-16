#pragma once
#include "../../common/input_state.hpp"

namespace orbit {
// Core-0 controller calls these at reset parameter enter/exit boundaries.
void matrix_arm_reset(InputState&) noexcept;
void matrix_finish_reset(InputState&, std::uint8_t& effects) noexcept;
void matrix_follow_played_pad(InputState&, bool enabled) noexcept;
// True while owed note-offs remain undelivered because the batch off buffer was full.
// The controller must keep calling process_matrix_input (an empty frame is enough) until this is false.
bool matrix_release_pending(const InputState&) noexcept;
}  // namespace orbit
