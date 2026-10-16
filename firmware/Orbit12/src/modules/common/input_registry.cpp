#include "input_registry.hpp"

// input_module_at() and input_module_count() are generated (modules/generated/input_registry_generated.cpp).
namespace orbit {
namespace {
bool equal(const char* a, const char* b) noexcept {
  if (!a || !b) return false;
  while (*a && *b) if (*a++ != *b++) return false;
  return *a == *b;
}
}
const InputDescriptor* find_input_module(const char* id) noexcept {
  for (std::uint8_t i = 0; i < input_module_count(); ++i) {
    const auto* d = input_module_at(i);
    if (d && equal(id, d->id)) return d;
  }
  return nullptr;
}
}  // namespace orbit
