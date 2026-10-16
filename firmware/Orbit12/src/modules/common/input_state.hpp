#pragma once

#include "../../domain/types.hpp"

namespace orbit {
struct InputRelationships {
  static constexpr std::uint8_t kCapacity = 12;
  std::uint8_t input[kCapacity]{};
  std::uint8_t output[kCapacity]{};
  std::uint8_t count = 0;
  bool add(std::uint8_t in, std::uint8_t out) noexcept;
  std::uint8_t remove_all(std::uint8_t in, std::uint8_t* removed, std::uint8_t cap) noexcept;
  bool has_output(std::uint8_t out) const noexcept;
  void clear() noexcept { count = 0; }
};
struct NoteInputState {
  std::uint8_t layout = 0;
  std::uint8_t velocity = 127;
  InputRelationships relationships{};
  std::uint32_t relationship_overflow = 0;
};
struct DrumInputState {
  std::uint8_t layout = 0;
  std::uint8_t base_note = 36;
  std::uint8_t velocity = 127;
  InputRelationships relationships{};
  std::uint32_t relationship_overflow = 0;
};
struct ChordInputState {
  std::uint8_t modifier_pad = 0;
  std::uint8_t layout = 0;
  std::uint8_t split_drop = 7;
  std::uint8_t bass_mode = 0;
  std::uint8_t spread_mode = 0;
  std::uint8_t borrow_scale = 0;
  std::uint8_t max_chords = 1;
  std::uint8_t velocity = 127;
  std::uint8_t modifier_actions[6]{4,7,8,5,1,10};
  std::uint8_t held_modifiers[6]{};
  std::uint8_t held_modifier_count = 0;
  InputRelationships relationships{};
  std::uint8_t held_chords = 0;
  std::uint8_t temp_chord[6]{};
  std::uint8_t temp_chord_count = 0;
  // Mirrors source follow_modifier_input: active_chain == INPUT_SLOT and
  // active_ui_section is UI_PARMSELECTION (1) or UI_PARMEDIT (2).
  bool follow_modifier_keys = false;
  std::uint32_t relationship_overflow = 0;
  std::uint32_t output_overflow = 0;
};
struct MatrixRelationships {
  static constexpr std::uint8_t kCapacity = 36;
  std::uint8_t input[kCapacity]{};
  std::uint8_t output[kCapacity]{};
  std::uint8_t count = 0;
  bool add(std::uint8_t in,std::uint8_t out) noexcept;
  std::uint8_t remove_all(std::uint8_t in,std::uint8_t* removed,std::uint8_t cap) noexcept;
  bool has_output(std::uint8_t out) const noexcept;
  void clear() noexcept { count=0; }
};
struct MatrixInputState {
  std::uint8_t pad=0;
  std::uint8_t bass_mode=0;
  std::uint8_t spread_mode=0;
  std::int32_t notes[12]{1,1,2,3,3,4,5,5,6,7,7,8};
  std::uint8_t chords[12]{1,0,0,1,0,0,1,0,0,1,0,0};
  std::uint8_t sevenths[12]{};
  std::uint8_t ninths[12]{};
  std::uint8_t inversions[12]{};
  std::uint8_t sus[12]{};
  std::uint8_t power[12]{};
  std::uint8_t dim[12]{};
  MatrixRelationships relationships{};
  std::uint8_t temp_notes[6]{};
  std::uint8_t temp_count=0;
  std::uint8_t velocity=127;
  std::uint8_t reset_value=0;
  bool reset_armed=false;
  // Mirrors source should_follow_played_pad: active input slot and parameter
  // selection/edit section, set by the core-0 controller.
  bool follow_played_pad=false;
  std::uint32_t relationship_overflow=0;
  std::uint32_t output_overflow=0;
};
// Source: inputmodules/note_walk.py (NoteWalk). Held pads are logical pads 0..11.
struct NoteWalkInputState {
  std::uint8_t held_pads[kKeys]{};
  std::uint8_t held_velocities[kKeys]{};
  std::uint8_t held_count = 0;
  std::int16_t offset_root = -1;          // -1 mirrors Python None
  std::int64_t note_root = 0;             // convert_note result, unclamped
  bool has_note_root = false;
  std::int64_t current_steps = 0;
  std::int16_t active_note = -1;          // -1 mirrors Python None
  std::uint8_t velocity = 127;
  std::uint32_t output_overflow = 0;
};

// Source: inputmodules/chord_walk.py (ChordWalk). Pad indices 0..3 are chord slots.
struct ChordWalkInputState {
  static constexpr std::uint8_t kPadCount = 4;
  static constexpr std::uint8_t kTriggerCount = 8;
  static constexpr std::uint8_t kRegisterCapacity = 6;
  static constexpr std::size_t kPendingOffCapacity = 4 * kInputEventCapacity;

  std::uint8_t pad = 0;
  bool preview_chord = false;
  std::int32_t notes[kPadCount]{1, 3, 5, 7};
  std::uint8_t sevenths[kPadCount]{};
  std::uint8_t sus[kPadCount]{};
  std::uint8_t power[kPadCount]{};
  std::uint8_t held_chords[kPadCount]{};
  std::uint8_t held_chord_count = 0;
  std::uint8_t held_triggers[kTriggerCount]{};
  std::uint8_t held_trigger_velocities[kTriggerCount]{};
  std::uint8_t held_trigger_count = 0;
  std::uint8_t register_notes[kRegisterCapacity]{};
  std::uint8_t register_count = 0;
  std::int16_t trigger_root = -1;         // -1 mirrors Python None
  std::int64_t current_step = 0;
  std::int16_t active_note = -1;          // -1 mirrors Python None
  std::uint8_t velocity = 127;
  // Preview note-offs owed from chord releases; drained before any new batch work.
  std::uint8_t pending_off[kPendingOffCapacity]{};
  std::uint16_t pending_off_count = 0;
  // Python __init__ forces the global key_offset to -2; the controller applies it.
  bool key_offset_request_pending = false;
  // Mirrors source should_follow_played_pad (set by core-0 controller).
  bool follow_played_pad = false;
  // Python builds the initial register in __init__ with the then-current global;
  // here it is built on the first process call with the global view.
  bool initial_register_pending = false;
  std::uint32_t output_overflow = 0;
};

// The union of input state structs is generated from module.json (modules/generated/input_state_generated.hpp).
}  // namespace orbit
#include "../generated/input_state_generated.hpp"
namespace orbit {

struct InputState {
  InputStorage storage;
  constexpr InputState() : storage{} {}
};
void construct_note_input(InputState&) noexcept;
void construct_drum_input(InputState&) noexcept;
void construct_chords_input(InputState&) noexcept;
void construct_matrix_input(InputState&) noexcept;
void process_note_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void process_drum_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void process_chords_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void process_matrix_input(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&) noexcept;
void reset_note_input(InputState&, ResetReason) noexcept;
void reset_drum_input(InputState&, ResetReason) noexcept;
void reset_chords_input(InputState&, ResetReason) noexcept;
void reset_matrix_input(InputState&, ResetReason) noexcept;
bool set_note_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
bool set_drum_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
bool set_chords_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
bool set_matrix_input_param(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
void set_chords_modifier_follow(InputState&, bool enabled) noexcept;
void set_matrix_pad_follow(InputState&, bool enabled) noexcept;
void arm_matrix_reset(InputState&) noexcept;
void finish_matrix_reset(InputState&, std::uint8_t& effects) noexcept;
void no_input_attributes(InputState&, AttributeVisitor&) noexcept;
const InputDescriptor& note_input_descriptor() noexcept;
const InputDescriptor& drum_input_descriptor() noexcept;
const InputDescriptor& chords_input_descriptor() noexcept;
const InputDescriptor& matrix_input_descriptor() noexcept;
}  // namespace orbit
