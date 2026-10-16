// Generated registry equality (modules/generated, tools/modules/gen_registry.py).
// The expected values below were copied from the hand-written tables before generation
// (registry.cpp switch order, input_registry.cpp switch order, ui_model selector labels,
// module_descriptors.hpp table, and the ModuleState sizes measured at that time).
#include "modules/generated/module_counts_generated.hpp"
#include <cstdio>
#include <cstring>

#include "app/module_descriptors.hpp"
#include "app/ui_model.hpp"
#include "modules/common/input_registry.hpp"
#include "modules/common/module_state.hpp"
#include "modules/common/registry.hpp"
#include "persistence/scene_schema.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
bool same(const char* a, const char* b) { return a != nullptr && b != nullptr && std::strcmp(a, b) == 0; }

// Previous hand order (registry.cpp, module_at case 0..17) and selector labels (ui_model.cpp).
constexpr const char* kProcessorIds[] = {"empty", "arp", "arp_walk", "bounce", "chord", "chopper", "delay",
    "drop", "euclid", "gate", "latch", "pick", "quantize", "randomize", "retrigger", "strum", "transpose", "wander"};
constexpr const char* kProcessorLabels[] = {"EMPTY", "ARP", "ARPWLK", "BOUNCE", "CHRD", "CHOPPR", "DELAY", "DROP",
    "EUCLID", "GATE", "LATCH", "PICK", "QNT", "RND", "RTRGR", "STRUM", "TRNS", "WANDR"};
// Previous input order (input_registry.cpp) and selector labels.
constexpr const char* kInputIds[] = {"note", "drum", "chords", "matrix", "note_walk", "chord_walk"};
constexpr const char* kInputSelectorIds[] = {"note", "chords", "drum", "matrix", "note_walk", "chord_walk"};
constexpr const char* kInputLabels[] = {"NOTE", "CHRD", "DRUM", "MTRX", "NOTEWLK", "CHRDWLK"};
// Previous footer descriptions (module_descriptors.hpp table).
struct Expected { const char* id; const char* description; };
constexpr Expected kDescriptions[] = {
    {"note", "PADS IN SCALE"}, {"chords", "CHORD ROOTS+MODS"}, {"drum", "DRUM PAD LAYOUT"},
    {"matrix", "CUSTOM PAD MAP"}, {"note_walk", "WALKING NOTE LINE"}, {"chord_walk", "CHORD TONE WALK"},
    {"empty", "NO PROCESSING"}, {"arp", "ARPEGGIATES NOTES"}, {"arp_walk", "ARP WALK PATTERN"},
    {"bounce", "REPEATS, DECAYS"}, {"chord", "STACKS INTERVALS"}, {"chopper", "CHOPS HELD NOTES"},
    {"delay", "DELAYS NOTES"}, {"drop", "DROPS NOTE-ONS"}, {"euclid", "EUCLIDEAN RHYTHM"},
    {"gate", "SETS NOTE LENGTH"}, {"latch", "HOLDS NOTES"}, {"pick", "PICKS SUBSET"},
    {"quantize", "SNAPS TO GRID"}, {"randomize", "RANDOM VARIATION"}, {"retrigger", "RETRIGGERS NOTES"},
    {"strum", "ROLLS CHORDS"}, {"transpose", "SHIFTS PITCH"}, {"wander", "RANDOM WALK LINE"}};
}  // namespace

int main() {
  check(orbit::module_count() == orbit::kGeneratedProcessorCount, "release processor count matches the generated count");
  for (std::uint8_t i = 0; i < 18; ++i) {
    const auto* d = orbit::module_at(i);
    check(d != nullptr && same(d->id, kProcessorIds[i]), "processor registry order");
    check(same(orbit::processor_selector(i).id, kProcessorIds[i]), "processor selector order");
    check(same(orbit::processor_selector(i).label, kProcessorLabels[i]), "processor selector label");
  }
  check(orbit::module_at(orbit::kGeneratedProcessorCount) == nullptr, "no processor past the registry");
  check(orbit::find_module("phraser") == nullptr, "phraser stays hidden");
  // Core scene keys equal their module ids (existing scenes depend on it); fixed slots pass through.
  for (const char* id : kProcessorIds) check(same(orbit::module_scene_key(id), id), "core scene key is the id");
  check(same(orbit::module_scene_key("set"), "set") && same(orbit::module_scene_key("settings"), "settings"),
        "fixed slot names pass through");
  check(orbit::input_module_count() == 6, "input count is 6");
  for (std::uint8_t i = 0; i < 6; ++i) {
    const auto* d = orbit::input_module_at(i);
    check(d != nullptr && same(d->id, kInputIds[i]), "input registry order");
  }
  for (std::uint8_t i = 0; i < 6; ++i) {
    check(same(orbit::input_selector(i).id, kInputSelectorIds[i]), "input selector order");
    check(same(orbit::input_selector(i).label, kInputLabels[i]), "input selector label");
  }
  check(orbit::kModuleDescriptionCount == orbit::kGeneratedProcessorCount + orbit::kGeneratedInputCount, "descriptor count is processors plus inputs");
  for (const auto& expected : kDescriptions) {
    const char* text = orbit::module_descriptor(expected.id);
    check(same(text, expected.description), "descriptor text");
  }
  // SCN1 sizes: the generated layout equals the hand values (static_asserts in scene_schema.hpp);
  // this test repeats the numbers so a change shows up as a named failure here too.
  check(orbit::kSceneBaseRecordLines == 159 && orbit::kSceneBankValues == 170 && orbit::kSceneMaxBytes == 38637,
        "scene bounds");
  check(orbit::kSceneInputLayout[3].parameters == 17 && orbit::kSceneInputLayout[3].attribute_values == 96,
        "matrix layout");
  // ModuleState sizes (bytes) as measured before generation.
  check(sizeof(orbit::DelayModuleState) == 632, "delay state bytes");
  check(sizeof(orbit::ArpModuleState) == 264, "arp state bytes");
  check(sizeof(orbit::WanderModuleState) == 144, "wander state bytes");
  check(sizeof(orbit::EmptyModuleState) == 1, "empty state bytes");
  check(sizeof(orbit::ModuleState) == 632, "ModuleState is 632 bytes (generated union, unchanged)");
  check(sizeof(orbit::InputState) == 344, "InputState is 344 bytes (generated union, unchanged)");
  if (failures == 0) std::printf("t16_registry_equality: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
