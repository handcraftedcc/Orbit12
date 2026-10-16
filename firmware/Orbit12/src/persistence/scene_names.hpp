#pragma once

#include <cstddef>
#include <cstdint>

#include "../domain/types.hpp"

namespace orbit {

// Named scene slots. Slot files are Scene01..Scene16 (1-based file numbers, slot index
// 0..15). The names file "scene_names" holds one line per occupied slot:
//   "NN,NAME\n"   NN = two decimal digits 01..16 (1-based, matches Scene01), NAME = 1..8 chars
//                 from A-Z and 0-9. CRLF line ends are accepted when parsing.
// A slot is occupied exactly when the names file has an entry for it. Names are unique
// across slots (case-sensitive; only upper-case names can exist).
constexpr std::size_t kSceneNameMaxChars = 8;
constexpr std::size_t kSceneNameBytes = kSceneNameMaxChars + 1;  // NUL-terminated buffer
constexpr std::size_t kSceneNamesLineChars = 2 + 1 + kSceneNameMaxChars;  // "NN,NAME"
constexpr std::size_t kSceneNamesMaxBytes = 256;  // 16 lines of at most 13 bytes, with headroom

enum class SceneNamesResult : std::uint8_t {
  Ok,
  TooLarge,    // input longer than kSceneNamesMaxBytes (output unchanged)
  OutputFull   // encoder output buffer too small
};

struct SceneNames {
  char names[kSceneSlotCount][kSceneNameBytes]{};
  std::uint16_t occupied_mask = 0;   // bit i set: slot i occupied
  // Lines skipped by the last parse_scene_names (bad shape, bad name, out-of-range or
  // duplicate slot, duplicate name). Diagnostics only; never fatal.
  std::uint32_t malformed_lines = 0;

  bool occupied(std::size_t slot) const noexcept {
    return slot < kSceneSlotCount && ((occupied_mask >> slot) & 1U) != 0U;
  }
  // Empty string for an unoccupied or out-of-range slot.
  const char* name(std::size_t slot) const noexcept {
    return occupied(slot) ? names[slot] : "";
  }
  std::size_t occupied_count() const noexcept;
  void clear() noexcept;
  // Stores a validated name in slot (caller checks name_valid/name_taken first).
  void set(std::size_t slot, const char* text) noexcept;
};

// [A-Z0-9], 1..8 characters, NUL-terminated input.
bool name_valid(const char* text) noexcept;
bool name_valid(const char* text, std::size_t length) noexcept;
// Exact match against any occupied slot's name.
bool name_taken(const SceneNames& names, const char* text) noexcept;
// Lowest unoccupied slot index.
bool first_free_slot(const SceneNames& names, std::uint8_t& slot) noexcept;
// Lowest occupied slot index.
bool first_occupied_slot(const SceneNames& names, std::uint8_t& slot) noexcept;
// Next unused NATO word (ALFA, BRAVO, ...). Exhaustion (not reachable with 16 slots and
// 26 words) falls back to a bounded "<word truncated>NN" cycle, suffix 2..99.
// out must hold kSceneNameBytes bytes. Returns false only if nothing is free.
bool suggest_name(const SceneNames& names, char* out, std::size_t capacity) noexcept;

// Parses the whole names text into `out` (cleared first). Malformed lines are skipped and
// counted in out.malformed_lines; the parse never fails on content. Blank lines are not
// counted. Input longer than kSceneNamesMaxBytes is rejected with TooLarge and out is kept.
SceneNamesResult parse_scene_names(const char* data, std::size_t length,
                                   SceneNames& out) noexcept;
// Writes the canonical text (LF line ends, slots ascending).
SceneNamesResult encode_scene_names(const SceneNames& names, char* output,
                                    std::size_t capacity, std::size_t& written) noexcept;

// Active-scene pointer text: "N\n" or "NN\n" (CRLF accepted), N = 1..16, 1-based.
// Returns the 0-based slot through slot. False for any other text.
bool parse_active_pointer(const char* text, std::size_t count, std::uint8_t& slot) noexcept;
// Writes "N\n" / "NN\n" for a 0-based slot. Returns length (0 for an out-of-range slot).
std::size_t format_active_pointer(std::uint8_t slot, char* output, std::size_t capacity) noexcept;

}  // namespace orbit
