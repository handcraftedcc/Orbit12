#pragma once
// Footer descriptions for the module selection view (17 cells of the 6px font).
// Source and rationale for each entry: docs/migration/module-descriptors.md.
// Every description uses only characters in the renderer font (FONTCHARS).
#include <cstddef>
#include <cstdint>
#include "../modules/generated/module_counts_generated.hpp"  // kGeneratedProcessorCount, kGeneratedInputCount

namespace orbit {

struct ModuleDescription {
  const char* id;           // Registry id (modules/common registry, input_registry)
  const char* label;        // Selector label shown by ui_model module_label_for
  const char* description;  // At most 17 characters
};

// Generated table (modules/generated/descriptors_generated.hpp): kModuleDescriptionCount and
// kModuleDescriptions, from the module.json manifests. Do not edit here.
#include "../modules/generated/descriptors_generated.hpp"
inline constexpr std::size_t kModuleDescriptionMaxLength = 17;

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
