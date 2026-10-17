// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Test-profile input state union (core plus examples). Generated from module.json state entries.
// Include modules/common/module_state.hpp or input_state.hpp (this file depends on the core structs there).
#pragma once
#include <cstddef>
#include <cstdint>

#include "../../../modules/examples/example/pad_velocity/pad_velocity_state.hpp"

namespace orbit {
union InputStorage {
    NoteInputState note;
    DrumInputState drum;
    ChordInputState chords;
    MatrixInputState matrix;
    NoteWalkInputState note_walk;
    ChordWalkInputState chord_walk;
    PadVelocityInputState user_example_pad_velocity;
    constexpr InputStorage() : note{} {}
};
static_assert(sizeof(NoteInputState) == 32, "note: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(DrumInputState) == 32, "drum: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ChordInputState) == 64, "chords: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(MatrixInputState) == 228, "matrix: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(NoteWalkInputState) == 64, "note_walk: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ChordWalkInputState) == 344, "chord_walk: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(PadVelocityInputState) == 44, "user.example.pad_velocity: state.bytes in module.json disagrees with sizeof");
inline constexpr std::size_t kGeneratedInputStateMaxBytes = 344;  // core plus modules compiled into the tree
static_assert(sizeof(InputStorage) >= kGeneratedInputStateMaxBytes, "union smaller than a member");
static_assert(sizeof(InputStorage) - kGeneratedInputStateMaxBytes < alignof(InputStorage), "union padding exceeds its alignment");
static_assert(alignof(InputStorage) <= alignof(std::int64_t), "state alignment above the chain budget");
}  // namespace orbit
