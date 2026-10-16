#pragma once
// Footer descriptions for the module selection view (17 cells of the 6px font).
// Source and rationale for each entry: docs/migration/module-descriptors.md.
// Every description uses only characters in the renderer font (FONTCHARS).
#include <cstddef>
#include <cstdint>

namespace orbit {

struct ModuleDescription {
  const char* id;           // Registry id (domain/modules registry, input_registry)
  const char* label;        // Selector label shown by ui_model module_label_for
  const char* description;  // At most 17 characters
};

inline constexpr std::size_t kModuleDescriptionCount = 24;  // 18 processors + 6 inputs
inline constexpr std::size_t kModuleDescriptionMaxLength = 17;

inline constexpr ModuleDescription kModuleDescriptions[kModuleDescriptionCount] = {
    // Inputs (first chain slot)
    {"note", "NOTE", "PADS IN SCALE"},
    {"chords", "CHRD", "CHORD ROOTS+MODS"},
    {"drum", "DRUM", "DRUM PAD LAYOUT"},
    {"matrix", "MTRX", "CUSTOM PAD MAP"},
    {"note_walk", "NOTEWLK", "WALKING NOTE LINE"},
    {"chord_walk", "CHRDWLK", "CHORD TONE WALK"},
    // Processors
    {"empty", "EMPTY", "NO PROCESSING"},
    {"arp", "ARP", "ARPEGGIATES NOTES"},
    {"arp_walk", "ARPWLK", "ARP WALK PATTERN"},
    {"bounce", "BOUNCE", "REPEATS, DECAYS"},
    {"chord", "CHRD", "STACKS INTERVALS"},
    {"chopper", "CHOPPR", "CHOPS HELD NOTES"},
    {"delay", "DELAY", "DELAYS NOTES"},
    {"drop", "DROP", "DROPS NOTE-ONS"},
    {"euclid", "EUCLID", "EUCLIDEAN RHYTHM"},
    {"gate", "GATE", "SETS NOTE LENGTH"},
    {"latch", "LATCH", "HOLDS NOTES"},
    {"pick", "PICK", "PICKS SUBSET"},
    {"quantize", "QNT", "SNAPS TO GRID"},
    {"randomize", "RND", "RANDOM VARIATION"},
    {"retrigger", "RTRGR", "RETRIGGERS NOTES"},
    {"strum", "STRUM", "ROLLS CHORDS"},
    {"transpose", "TRNS", "SHIFTS PITCH"},
    {"wander", "WANDR", "RANDOM WALK LINE"},
};

namespace module_descriptor_detail {
inline bool same(const char* a, const char* b) noexcept {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) return false;
    ++a;
    ++b;
  }
  return *a == *b;
}
// Number of table entries whose label equals `label`.
inline std::size_t label_matches(const char* label) noexcept {
  std::size_t count = 0;
  for (const ModuleDescription& entry : kModuleDescriptions) {
    if (same(entry.label, label)) ++count;
  }
  return count;
}
}  // namespace module_descriptor_detail

// Returns the footer description for a module. The id is the primary key. A
// label is accepted only when it names exactly one entry; "CHRD" labels both
// the chord processor and the chords input, so callers must pass the id there.
// Returns nullptr for unknown or ambiguous keys.
inline const char* module_descriptor(const char* id_or_label) noexcept {
  if (id_or_label == nullptr) return nullptr;
  for (const ModuleDescription& entry : kModuleDescriptions) {
    if (module_descriptor_detail::same(entry.id, id_or_label)) return entry.description;
  }
  if (module_descriptor_detail::label_matches(id_or_label) != 1) return nullptr;
  for (const ModuleDescription& entry : kModuleDescriptions) {
    if (module_descriptor_detail::same(entry.label, id_or_label)) return entry.description;
  }
  return nullptr;
}

}  // namespace orbit
