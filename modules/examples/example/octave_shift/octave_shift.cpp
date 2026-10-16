// Example user processor. Shifts each note up 12 semitones, clamped to MIDI 127.
// The shift is a pure function of the pitch, so a note-off releases exactly the pitch its note-on sounded.
#include "octave_shift.hpp"

#include <cstdint>

namespace orbit {
namespace {
constexpr int kOctaveSemitones = 12;

std::uint8_t shifted(std::uint8_t pitch) noexcept {
  const int value = static_cast<int>(pitch) + kOctaveSemitones;
  return static_cast<std::uint8_t>(value > kMidiMax ? kMidiMax : value);
}
void construct_octave_shift(ModuleState&) noexcept {}
void reset_octave_shift(ModuleState&, ResetReason) noexcept {}
void destroy_octave_shift(ModuleState&) noexcept {}
void octave_shift_attributes(ModuleState&, AttributeVisitor&) noexcept {}
void process_octave_shift(ModuleState&, const ProcessContext&, const NoteBatch& input, NoteBatch& output) noexcept {
  output = {};
  // A full output buffer sets the sticky overflow flag, which the chain turns into a fault (all notes off).
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const auto& note = input.on.items[i];
    (void)output.on.append({shifted(note.pitch), note.velocity, note.channel});
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const auto& note = input.off.items[i];
    (void)output.off.append({shifted(note.pitch), note.velocity, note.channel});
  }
}
const ModuleDescriptor kOctaveShiftDescriptor{"user.example.octave_shift", 1, nullptr, 0,
    construct_octave_shift, destroy_octave_shift, process_octave_shift, reset_octave_shift,
    nullptr, octave_shift_attributes};
}  // namespace

const ModuleDescriptor& user_example_octave_shift_module_descriptor() noexcept { return kOctaveShiftDescriptor; }
}  // namespace orbit
