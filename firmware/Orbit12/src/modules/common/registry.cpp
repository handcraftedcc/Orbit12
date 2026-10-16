#include "registry.hpp"

// module_at() and module_count() are generated (modules/generated/registry_generated.cpp) from the
// module.json manifests. This file keeps only the id lookup.
namespace orbit {
namespace {
bool equal(const char* a, const char* b) noexcept {
  if (!a || !b) return false;
  while (*a && *b) if (*a++ != *b++) return false;
  return *a == *b;
}
}
const ModuleDescriptor* find_module(const char* id) noexcept {
  for (std::uint8_t i = 0; i < module_count(); ++i) {
    const auto* descriptor = module_at(i);
    if (!id || !descriptor || !descriptor->id) continue;
    if (equal(id, descriptor->id)) return descriptor;
  }
  return nullptr;
}
}  // namespace orbit
