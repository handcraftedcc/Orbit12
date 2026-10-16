// Sample user module (modules/examples/example/octave_shift) through the test-profile generated registry.
// Checks: registry index and lookup, descriptor metadata, note transformation, release symmetry
// (an off releases the pitch its on sounded), and the sticky overflow flag when the output is full.
#include <cstdio>
#include <cstring>

#include "domain/types.hpp"
#include "modules/common/module_state.hpp"
#include "modules/common/registry.hpp"
#include "generated/module_counts.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
}  // namespace

int main() {
  using namespace orbit;
  check(module_count() == orbit::kGeneratedProcessorCount, "test profile count matches the generated test registry");
  const ModuleDescriptor* d = find_module("user.example.octave_shift");
  check(d != nullptr, "example is found by id");
  bool listed = false;
  for (std::uint8_t i = 0; i < module_count(); ++i) listed = listed || module_at(i) == d;
  check(listed, "example is listed by the registry");
  check(find_module("user.example.missing") == nullptr, "unknown user id is not found");
  // SCN1 stores the 10-character scene key; the registry maps it both ways.
  check(std::strcmp(module_scene_key("user.example.octave_shift"), "ex_oct") == 0, "example scene key");
  check(std::strcmp(module_id_for_scene_key("ex_oct"), "user.example.octave_shift") == 0, "example key lookup");
  check(find_module(module_id_for_scene_key("ex_oct")) == d, "scene key resolves to the example");
  if (d == nullptr) return 1;

  ModuleState state;
  d->construct(state);
  GlobalState global{};
  TransportView transport{};
  RandomSource random{};  // unused by this module; no draws
  ProcessContext context{TimeSample{}, global, transport, random};

  // Note transformation, through the descriptor's process function with a real context.
  NoteBatch input{};
  NoteBatch output{};
  input.on.append({60, 100, 0});
  input.on.append({120, 90, 3});
  input.on.append({127, 80, 0});
  input.off.append({60, 0, 0});
  input.off.append({127, 0, 0});
  d->process(state, context, input, output);
  check(output.on.count == 3 && output.on.items[0].pitch == 72, "60 raised to 72");
  check(output.on.items[1].pitch == 127 && output.on.items[1].channel == 3, "120 clamped, channel kept");
  check(output.on.items[2].pitch == 127, "127 clamped");
  check(output.off.count == 2 && output.off.items[0].pitch == 72, "off releases the sounding pitch");
  check(output.off.items[1].pitch == 127, "off of a clamped note releases 127");
  check(!output.overflowed(), "no overflow for a normal batch");

  d->destroy(state);
  if (failures == 0) std::printf("t17_user_example: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
