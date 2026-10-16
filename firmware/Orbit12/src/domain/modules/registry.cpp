#include "registry.hpp"
#include "drop_randomize.hpp"
#include "gate_chopper_retrigger.hpp"
#include "delay_bounce.hpp"
#include "euclid.hpp"
#include "arp.hpp"
#include "arp_walk.hpp"
#include "latch_pick.hpp"
#include "quantize_strum.hpp"
#include "wander.hpp"

namespace orbit {
namespace {
bool equal(const char* a, const char* b) noexcept {
  if (!a || !b) return false;
  while (*a && *b) if (*a++ != *b++) return false;
  return *a == *b;
}
}
const ModuleDescriptor& empty_module_descriptor() noexcept;
const ModuleDescriptor* module_at(std::uint8_t index) noexcept {
  // Source order: registry._registry.MODULE_SPECS, phraser (no implementation) hidden.
  switch (index) {
    case 0: return &empty_module_descriptor();
    case 1: return &arp_module_descriptor();
    case 2: return &arp_walk_module_descriptor();
    case 3: return &bounce_module_descriptor();
    case 4: return &chord_module_descriptor();
    case 5: return &chopper_module_descriptor();
    case 6: return &delay_module_descriptor();
    case 7: return &drop_module_descriptor();
    case 8: return &euclid_module_descriptor();
    case 9: return &gate_module_descriptor();
    case 10: return &latch_module_descriptor();
    case 11: return &pick_module_descriptor();
    case 12: return &quantize_module_descriptor();
    case 13: return &randomize_module_descriptor();
    case 14: return &retrigger_module_descriptor();
    case 15: return &strum_module_descriptor();
    case 16: return &transpose_module_descriptor();
    case 17: return &wander_module_descriptor();
    default: return nullptr;
  }
}
std::uint8_t module_count() noexcept { return 18; }
const ModuleDescriptor* find_module(const char* id) noexcept {
  for (std::uint8_t i = 0; i < module_count(); ++i) {
    const auto* descriptor = module_at(i);
    if (!id || !descriptor || !descriptor->id) continue;
    if (equal(id, descriptor->id)) return descriptor;
  }
  return nullptr;
}
}  // namespace orbit
