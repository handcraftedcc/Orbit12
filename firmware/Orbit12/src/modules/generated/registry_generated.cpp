// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Source: module.json manifests (core order, then user modules sorted by id).
// Native tests define ORBIT_NATIVE_CORE_ONLY (tests/native/CMakeLists.txt): user modules are then left out.
#include <cstdint>
#include <cstring>
#include "../../domain/types.hpp"
#include "../common/registry.hpp"
#include "module_counts_generated.hpp"

#include "../common/module_state.hpp"
#include "../core/arp/arp.hpp"
#include "../core/arp_walk/arp_walk.hpp"
#include "../core/delay/delay_bounce.hpp"
#include "../core/drop/drop_randomize.hpp"
#include "../core/euclid/euclid.hpp"
#include "../core/gate/gate_chopper_retrigger.hpp"
#include "../core/latch/latch_pick.hpp"
#include "../core/quantize/quantize_strum.hpp"
#include "../core/wander/wander.hpp"

namespace orbit {
const ModuleDescriptor& empty_module_descriptor() noexcept;
const ModuleDescriptor& arp_module_descriptor() noexcept;
const ModuleDescriptor& arp_walk_module_descriptor() noexcept;
const ModuleDescriptor& bounce_module_descriptor() noexcept;
const ModuleDescriptor& chord_module_descriptor() noexcept;
const ModuleDescriptor& chopper_module_descriptor() noexcept;
const ModuleDescriptor& delay_module_descriptor() noexcept;
const ModuleDescriptor& drop_module_descriptor() noexcept;
const ModuleDescriptor& euclid_module_descriptor() noexcept;
const ModuleDescriptor& gate_module_descriptor() noexcept;
const ModuleDescriptor& latch_module_descriptor() noexcept;
const ModuleDescriptor& pick_module_descriptor() noexcept;
const ModuleDescriptor& quantize_module_descriptor() noexcept;
const ModuleDescriptor& randomize_module_descriptor() noexcept;
const ModuleDescriptor& retrigger_module_descriptor() noexcept;
const ModuleDescriptor& strum_module_descriptor() noexcept;
const ModuleDescriptor& transpose_module_descriptor() noexcept;
const ModuleDescriptor& wander_module_descriptor() noexcept;
const ModuleDescriptor* module_at(std::uint8_t index) noexcept {
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
std::uint8_t module_count() noexcept { return kGeneratedProcessorCount; }
namespace {
struct SceneKeyRow {
  const char* module_id;
  const char* scene_key;
};
constexpr SceneKeyRow kSceneKeyRows[] = {
    {"empty", "empty"},
    {"arp", "arp"},
    {"arp_walk", "arp_walk"},
    {"bounce", "bounce"},
    {"chord", "chord"},
    {"chopper", "chopper"},
    {"delay", "delay"},
    {"drop", "drop"},
    {"euclid", "euclid"},
    {"gate", "gate"},
    {"latch", "latch"},
    {"pick", "pick"},
    {"quantize", "quantize"},
    {"randomize", "randomize"},
    {"retrigger", "retrigger"},
    {"strum", "strum"},
    {"transpose", "transpose"},
    {"wander", "wander"},
    {"note", "note"},
    {"drum", "drum"},
    {"chords", "chords"},
    {"matrix", "matrix"},
    {"note_walk", "note_walk"},
    {"chord_walk", "chord_walk"},
};
}  // namespace
const char* module_scene_key(const char* module_id) noexcept {
  if (module_id == nullptr) return nullptr;
  for (const SceneKeyRow& row : kSceneKeyRows) {
    if (std::strcmp(row.module_id, module_id) == 0) return row.scene_key;
  }
  return module_id;  // fixed slots (set, output, settings) keep their names
}
const char* module_id_for_scene_key(const char* scene_key) noexcept {
  if (scene_key == nullptr) return nullptr;
  for (const SceneKeyRow& row : kSceneKeyRows) {
    if (std::strcmp(row.scene_key, scene_key) == 0) return row.module_id;
  }
  return scene_key;
}
}  // namespace orbit
