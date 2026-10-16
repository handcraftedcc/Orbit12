#include "input_registry.hpp"
#include "walk_inputs.hpp"

namespace orbit {
namespace {
bool equal(const char* a, const char* b) noexcept {
  if (!a || !b) return false;
  while (*a && *b) if (*a++ != *b++) return false;
  return *a == *b;
}
}
const InputDescriptor* input_module_at(std::uint8_t index) noexcept {
  switch (index) {
    case 0: return &note_input_descriptor();
    case 1: return &drum_input_descriptor();
    case 2: return &chords_input_descriptor();
    case 3: return &matrix_input_descriptor();
    case 4: return &note_walk_input_descriptor();
    case 5: return &chord_walk_input_descriptor();
    default: return nullptr;
  }
}
std::uint8_t input_module_count() noexcept { return 6; }
const InputDescriptor* find_input_module(const char* id) noexcept {
  for (std::uint8_t i = 0; i < input_module_count(); ++i) {
    const auto* d = input_module_at(i);
    if (d && equal(id, d->id)) return d;
  }
  return nullptr;
}
}  // namespace orbit
