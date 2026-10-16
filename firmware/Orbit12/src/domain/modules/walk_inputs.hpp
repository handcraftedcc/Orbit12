#pragma once

#include "inputs.hpp"

namespace orbit {
void construct_note_walk_input(InputState&) noexcept;
void construct_chord_walk_input(InputState&) noexcept;
void process_note_walk_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void process_chord_walk_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void reset_note_walk_input(InputState&, ResetReason) noexcept;
void reset_chord_walk_input(InputState&, ResetReason) noexcept;
bool set_note_walk_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
bool set_chord_walk_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
void chord_walk_attributes(InputState&, AttributeVisitor&) noexcept;
// Controller hook for the Python ChordWalk.__init__ global side effect.
// Returns true once, writing the requested PADOFS value.
bool chord_walk_take_key_offset_request(InputState&, std::int32_t& value) noexcept;
void set_chord_walk_pad_follow(InputState&, bool enabled) noexcept;
const InputDescriptor& note_walk_input_descriptor() noexcept;
const InputDescriptor& chord_walk_input_descriptor() noexcept;
}  // namespace orbit
