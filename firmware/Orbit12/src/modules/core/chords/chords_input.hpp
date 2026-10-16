#pragma once
#include "inputs.hpp"

namespace orbit {
// T08b's state is part of InputState's concrete union; this header names the
// source algorithm entry points without creating a subclass hierarchy.
// Core-0 controller enables this only for source UI_PARMSELECTION/UI_PARMEDIT
// while the active chain slot is INPUT_SLOT; no renderer state enters domain.
void chords_input_follow_modifier_keys(InputState&, bool enabled) noexcept;
}  // namespace orbit
