// T18 example-profile scene round trip through the controller: the sample processor
// (modules/examples/example/octave_shift, scene key ex_oct) and the sample input
// (modules/examples/example/pad_velocity, scene key ex_padvel) are configured from a scene file, saved,
// reset or switched away, and loaded again. Built with ORBIT_NATIVE_EXAMPLE_PROFILE, so the registries,
// scene layout and state union come from tests/native/generated/*_examples.
// Scene files hold scene keys (at most 10 characters); module ids are never written.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "t11b_support.hpp"

int g_failures = 0;

namespace {
using namespace t11b;

// ---- in-memory scene storage (same shape as the T11b fixture) ----
constexpr std::size_t kKinds = static_cast<std::size_t>(SceneFileKind::SceneNamesBackup) + 1, kScenes = kSceneSlotCount;
struct Cell { std::vector<char> bytes; bool exists = false; };
struct MemoryFs;
struct ReadCursor { MemoryFs* fs = nullptr; SceneFileId id{}; std::size_t pos = 0; };
struct WriteCursor { MemoryFs* fs = nullptr; SceneFileId id{}; };
struct MemoryFs {
  Cell files[kKinds][kScenes];
  ReadCursor reader{};
  WriteCursor writer{};
  Cell& cell(SceneFileId id) { return files[static_cast<std::uint8_t>(id.kind)][id.scene]; }
  std::string text(SceneFileKind kind, std::uint8_t scene) {
    const Cell& c = files[static_cast<std::uint8_t>(kind)][scene];
    return std::string(c.bytes.begin(), c.bytes.end());
  }
  void put(SceneFileKind kind, std::uint8_t scene, const std::string& t) {
    Cell& c = files[static_cast<std::uint8_t>(kind)][scene];
    c.bytes.assign(t.begin(), t.end());
    c.exists = true;
  }
};
SceneReadStatus fs_read(void* ctx, char& byte) {
  auto& cur = *static_cast<ReadCursor*>(ctx);
  auto& src = cur.fs->cell(cur.id);
  if (cur.pos >= src.bytes.size()) return SceneReadStatus::End;
  byte = src.bytes[cur.pos++];
  return SceneReadStatus::Byte;
}
SceneWriteStatus fs_write(void* ctx, const char* data, std::size_t size) {
  auto& cur = *static_cast<WriteCursor*>(ctx);
  auto& dst = cur.fs->cell(cur.id);
  dst.bytes.insert(dst.bytes.end(), data, data + size);
  return SceneWriteStatus::Written;
}
SceneStorageStatus fs_exists(void* ctx, SceneFileId id, bool& exists) {
  exists = static_cast<MemoryFs*>(ctx)->cell(id).exists;
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_open_read(void* ctx, SceneFileId id, SceneReader& reader) {
  auto& fs = *static_cast<MemoryFs*>(ctx);
  if (!fs.cell(id).exists) return SceneStorageStatus::Missing;
  fs.reader = {&fs, id, 0};
  reader = {&fs.reader, fs_read};
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_open_write(void* ctx, SceneFileId id, SceneWriter& writer) {
  auto& fs = *static_cast<MemoryFs*>(ctx);
  Cell& c = fs.cell(id);
  c.exists = true;
  c.bytes.clear();
  fs.writer = {&fs, id};
  writer = {&fs.writer, fs_write};
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_close(void*, SceneFileId) { return SceneStorageStatus::Ok; }
SceneStorageStatus fs_rename(void* ctx, SceneFileId from, SceneFileId to) {
  auto& fs = *static_cast<MemoryFs*>(ctx);
  Cell& s = fs.cell(from);
  Cell& d = fs.cell(to);
  if (!s.exists || d.exists) return SceneStorageStatus::Error;
  d.bytes = s.bytes;
  d.exists = true;
  s.bytes.clear();
  s.exists = false;
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_remove(void* ctx, SceneFileId id) {
  auto& c = static_cast<MemoryFs*>(ctx)->cell(id);
  if (!c.exists) return SceneStorageStatus::Missing;
  c.exists = false;
  c.bytes.clear();
  return SceneStorageStatus::Ok;
}
SceneStoragePort port_for(MemoryFs& fs) {
  SceneStoragePort p;
  p.context = &fs;
  p.exists = fs_exists;
  p.open_read = fs_open_read;
  p.open_write = fs_open_write;
  p.close = fs_close;
  p.rename = fs_rename;
  p.remove = fs_remove;
  return p;
}

// Scene 0 with the example input in slot IN and the example processor in slot P1. The values are not the
// defaults: velocity 64, transpose 5, a pad_vel attribute row, and out_channel 3 (one-based in the file).
const char* const kConfigured =
    "SCN1\n"
    "S,key,0\n"
    "S,scale,1\n"
    "S,octave,3\n"
    "S,key_offset,0\n"
    "S,bpm,120\n"
    "S,swing,0.0\n"
    "S,transport_mode,0\n"
    "M,0,set,1\n"
    "P,key,0\n"
    "P,scale,1\n"
    "P,bpm,120\n"
    "P,swing,0.0\n"
    "P,transport_mode,0\n"
    "M,1,ex_padvel,1\n"
    "P,velocity,64\n"
    "P,transpose,5\n"
    "A,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"
    "M,2,ex_oct,1\n"
    "P,operation_mode,0\n"
    "P,out_channel,3\n"
    "M,3,empty,1\n"
    "M,4,empty,1\n"
    "M,5,empty,1\n"
    "M,6,empty,1\n"
    "M,7,empty,1\n"
    "M,8,output,1\n"
    "P,out_ch,1\n"
    "P,print,0\n"
    "M,9,settings,1\n"
    "END\n";

struct SceneRig {
  std::unique_ptr<MemoryFs> fs = std::make_unique<MemoryFs>();
  std::unique_ptr<Rig> rig;
  SceneRig() {
    ControllerPorts ports;
    ports.storage = port_for(*fs);
    rig = std::make_unique<Rig>(ports);
    std::string names;
    for (int slot = 1; slot <= 10; ++slot) {
      char line[16];
      std::snprintf(line, sizeof(line), "%02d,S%02d\n", slot, slot);
      names += line;
    }
    fs->put(SceneFileKind::SceneNames, 0, names);
    (void)rig->c.scene_service().request(SceneOperation::LoadNames);
    run_until_idle();
  }
  Controller& c() { return rig->c; }
  bool run_until_idle(int max_steps = 400) {
    for (int i = 0; i < max_steps && c().scene_busy(); ++i) rig->step(1);
    return !c().scene_busy();
  }
  // Runs one requested scene operation to completion and reports whether it succeeded.
  bool finish(bool requested) {
    return requested && run_until_idle() && c().last_scene_status() == SceneServiceStatus::Succeeded;
  }
};

bool contains(const std::string& text, const char* line) { return text.find(line) != std::string::npos; }

// The sample input and processor as configured by kConfigured.
void check_configured(SceneRig& s) {
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "user.example.pad_velocity") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), "user.example.octave_shift") == 0);
  const ParamRow* velocity = s.c().find_row(kSlotIn, "velocity");
  const ParamRow* transpose = s.c().find_row(kSlotIn, "transpose");
  CHECK(velocity != nullptr && velocity->value_i == 64);
  CHECK(transpose != nullptr && transpose->value_i == 5);
  const auto& pad = s.c().input_state().storage.user_example_pad_velocity;
  CHECK(pad.pad_vel[0] == 10 && pad.pad_vel[3] == 40 && pad.pad_vel[11] == 120);
  // The live module state, not only the UI rows, holds the loaded values.
  CHECK(pad.velocity == 64 && pad.transpose == 5);
  CHECK(s.c().slot(kSlotP1).out_channel == 2);  // file value 3 (one-based); SlotRecord is zero-based
}

void configured_scene_loads() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, kConfigured);
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.finish(s.c().request_load_active()));
  check_configured(s);
}

// The active input is written as its M record (the input bank is only written for parked inputs).
void save_writes_scene_keys_for_active_modules() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, kConfigured);
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.finish(s.c().request_load_active()));
  CHECK(s.finish(s.c().request_scene_save()));
  const std::string saved = s.fs->text(SceneFileKind::Scene, 0);
  CHECK(contains(saved, "M,1,ex_padvel,1\n"));
  CHECK(contains(saved, "P,velocity,64\n"));
  CHECK(contains(saved, "P,transpose,5\n"));
  CHECK(contains(saved, "A,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"));
  CHECK(contains(saved, "M,2,ex_oct,1\n"));
  CHECK(contains(saved, "P,out_channel,3\n"));
  CHECK(saved.find("user.example") == std::string::npos);  // module ids never reach the file
}

// Scene 0 with the example input parked: slot IN holds note, and the BANK sections keep the sample input's
// edits (BANK,ex_padvel with BP and BA lines), as a save writes them for an input that is not active.
const char* const kParked =
    "SCN1\n"
    "S,key,0\n"
    "S,scale,1\n"
    "S,octave,3\n"
    "S,key_offset,0\n"
    "S,bpm,120\n"
    "S,swing,0.0\n"
    "S,transport_mode,0\n"
    "M,0,set,1\n"
    "P,key,0\n"
    "P,scale,1\n"
    "P,bpm,120\n"
    "P,swing,0.0\n"
    "P,transport_mode,0\n"
    "M,1,note,1\n"
    "P,octave,3\n"
    "P,key_offset,0\n"
    "P,velocity,127\n"
    "P,layout,0\n"
    "P,operation_mode,0\n"
    "P,out_channel,1\n"
    "M,2,ex_oct,1\n"
    "P,operation_mode,0\n"
    "P,out_channel,3\n"
    "M,3,empty,1\n"
    "M,4,empty,1\n"
    "M,5,empty,1\n"
    "M,6,empty,1\n"
    "M,7,empty,1\n"
    "M,8,output,1\n"
    "P,out_ch,1\n"
    "P,print,0\n"
    "M,9,settings,1\n"
    "BANK,ex_padvel\n"
    "BP,velocity,64\n"
    "BP,transpose,5\n"
    "BA,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"
    "END\n";

void parked_input_bank_round_trip() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, kParked);
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.finish(s.c().request_load_active()));
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(s.finish(s.c().request_scene_save()));
  std::string saved = s.fs->text(SceneFileKind::Scene, 0);
  CHECK(contains(saved, "M,1,note,1\n"));
  CHECK(contains(saved, "BANK,ex_padvel\n"));
  CHECK(contains(saved, "BP,velocity,64\n"));
  CHECK(contains(saved, "BP,transpose,5\n"));
  CHECK(contains(saved, "BA,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"));
  CHECK(saved.find("user.example") == std::string::npos);

  // Switching away and back keeps the parked bank; the saved file is the same bank again.
  CHECK(s.finish(s.c().request_scene_switch(1)));
  CHECK(s.finish(s.c().request_scene_switch(0)));
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(s.finish(s.c().request_scene_save()));
  CHECK(contains(s.fs->text(SceneFileKind::Scene, 0), "BP,transpose,5\n"));
  CHECK(contains(s.fs->text(SceneFileKind::Scene, 0), "BA,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"));
}

void reset_then_load_restores_state() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, kConfigured);
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.finish(s.c().request_load_active()));
  CHECK(s.finish(s.c().request_scene_save()));
  const std::string saved = s.fs->text(SceneFileKind::Scene, 0);

  // Reset: stock defaults, the sample modules are gone from the input and processor slots.
  CHECK(s.finish(s.c().request_scene_reset()));
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), "empty") == 0);

  // Load the saved file again: state is restored from the scene keys.
  s.fs->put(SceneFileKind::Scene, 0, saved);
  CHECK(s.finish(s.c().request_load_active()));
  check_configured(s);
}

void switch_away_and_back_restores_state() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, kConfigured);
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.finish(s.c().request_load_active()));
  CHECK(s.finish(s.c().request_scene_save()));

  // Switch to scene 1 (no file: stock defaults), then back to scene 0.
  CHECK(s.finish(s.c().request_scene_switch(1)));
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(s.finish(s.c().request_scene_switch(0)));
  check_configured(s);
  CHECK(s.finish(s.c().request_scene_save()));
  CHECK(contains(s.fs->text(SceneFileKind::Scene, 0), "A,pad_vel,10|20|30|40|50|60|70|80|90|100|110|120\n"));
}
// Behavior of the sample input's descriptor (fixed inputs, no scene involved). Pads are physical keys mapped
// through music::kPadMap: key 9 is logical pad 0 (MIDI 60), key 0 is logical pad 9 (MIDI 69).
void pad_velocity_behavior() {
  const InputDescriptor* d = find_input_module("user.example.pad_velocity");
  CHECK(d != nullptr);
  if (d == nullptr) return;
  auto state = std::make_unique<InputState>();
  d->construct(*state);
  GlobalState global{};
  TransportView transport{};
  RandomSource random{};
  ProcessContext context{TimeSample{}, global, transport, random};
  InputView view{};
  auto& s = state->storage.user_example_pad_velocity;
  std::uint8_t effects = 0;

  auto run = [&](std::uint8_t key, bool down, NoteBatch& out) {
    InputFrame frame{};
    KeyEvent event{};
    event.key = key;
    event.down = down;
    (void)frame.keys.append(event);
    out = {};
    d->process(*state, context, frame, out, view);
  };
  NoteBatch out{};

  // Defaults: velocity 100 with pad_vel 127 gives 100; pad 0 is MIDI 60.
  run(9, true, out);
  CHECK(out.on.count == 1 && out.on.items[0].pitch == 60 && out.on.items[0].velocity == 100);
  run(9, false, out);
  CHECK(out.off.count == 1 && out.off.items[0].pitch == 60 && out.on.count == 0);

  // Transpose changes while the pad is held: the release still matches the sounded pitch.
  run(9, true, out);
  CHECK(d->set_param(*state, 1, ParamValue::integer(2), global, effects) && effects == EffectNone);
  run(9, false, out);
  CHECK(out.off.count == 1 && out.off.items[0].pitch == 60);
  run(9, true, out);
  CHECK(out.on.count == 1 && out.on.items[0].pitch == 62);
  run(9, false, out);
  CHECK(out.off.count == 1 && out.off.items[0].pitch == 62);

  // Re-press without a release: the old pitch is released before the new note-on.
  run(9, true, out);
  run(9, true, out);
  CHECK(out.off.count == 1 && out.off.items[0].pitch == 62);
  CHECK(out.on.count == 1 && out.on.items[0].pitch == 62);
  run(9, false, out);

  // Velocity: the floor is 1 (a note-on never has velocity 0). pad_vel 1 and velocity 1 give 1*1/127 = 0.
  CHECK(d->set_param(*state, 0, ParamValue::integer(1), global, effects));
  s.pad_vel[0] = 1;
  run(9, true, out);
  CHECK(out.on.count == 1 && out.on.items[0].velocity == 1);
  run(9, false, out);

  // Output overflow: 64 presses of one pad need more notes than the batch holds, and the flag is sticky.
  NoteBatch big{};
  InputFrame frame{};
  for (int i = 0; i < 64; ++i) {
    KeyEvent event{};
    event.key = 9;
    event.down = true;
    (void)frame.keys.append(event);
  }
  d->process(*state, context, frame, big, view);
  CHECK(big.overflowed());
  d->destroy(*state);
}
}  // namespace

int main() {
  pad_velocity_behavior();
  configured_scene_loads();
  save_writes_scene_keys_for_active_modules();
  parked_input_bank_round_trip();
  reset_then_load_restores_state();
  switch_away_and_back_restores_state();
  if (g_failures == 0) std::printf("t18_user_scene_roundtrip: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
