// T11b scene actions through the controller: SETTINGS slot controls, SceneService
// lifecycle callbacks, schema/attribute consistency and the release requirements.
// Python-derived text: fixtures/t11b_traces.inc holds the scene files written by
// the real core/scenes.py save_scene for scripted UI sessions (oracle harness).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "t11b_support.hpp"

int g_failures = 0;
using namespace t11b;

namespace {
struct TraceStep { char kind; int a; int b; };
struct TraceExpect {
  int section, chain, parm, scroll, nav, swap, mode, octave, offset, running, tmode, selector, key, scale, count, notes_off;
  const char* slots;
  int nvalues;
  double values[20];
};
struct TraceScenario { const char* name; const TraceStep* steps; const TraceExpect* expect; std::size_t count; };
#include "fixtures/t11b_traces.inc"

// ---- in-memory scene storage (same shape as the T07b fixture) ----
constexpr std::size_t kKinds = 9, kScenes = 10;  // all SceneFileKind values (user settings included)
struct Cell { std::vector<char> bytes; bool exists = false; };
struct MemoryFs;
struct ReadCursor { MemoryFs* fs = nullptr; SceneFileId id{}; std::size_t pos = 0; };
struct WriteCursor { MemoryFs* fs = nullptr; SceneFileId id{}; };
struct MemoryFs {
  Cell files[kKinds][kScenes];
  ReadCursor reader{};
  WriteCursor writer{};
  std::uint32_t calls = 0, writes = 0;
  bool fail_open_write = false;
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
  ++cur.fs->writes;
  dst.bytes.insert(dst.bytes.end(), data, data + size);
  return SceneWriteStatus::Written;
}
SceneStorageStatus fs_exists(void* ctx, SceneFileId id, bool& exists) {
  auto& fs = *static_cast<MemoryFs*>(ctx); ++fs.calls; exists = fs.cell(id).exists; return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_open_read(void* ctx, SceneFileId id, SceneReader& reader) {
  auto& fs = *static_cast<MemoryFs*>(ctx); ++fs.calls;
  if (!fs.cell(id).exists) return SceneStorageStatus::Missing;
  fs.reader = {&fs, id, 0};
  reader = {&fs.reader, fs_read};
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_open_write(void* ctx, SceneFileId id, SceneWriter& writer) {
  auto& fs = *static_cast<MemoryFs*>(ctx); ++fs.calls;
  if (fs.fail_open_write) return SceneStorageStatus::Error;
  Cell& c = fs.cell(id);
  c.exists = true; c.bytes.clear();
  fs.writer = {&fs, id};
  writer = {&fs.writer, fs_write};
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_close(void* ctx, SceneFileId) { ++static_cast<MemoryFs*>(ctx)->calls; return SceneStorageStatus::Ok; }
SceneStorageStatus fs_rename(void* ctx, SceneFileId from, SceneFileId to) {
  auto& fs = *static_cast<MemoryFs*>(ctx); ++fs.calls;
  Cell& s = fs.cell(from); Cell& d = fs.cell(to);
  if (!s.exists || d.exists) return SceneStorageStatus::Error;
  d.bytes = s.bytes; d.exists = true; s.bytes.clear(); s.exists = false;
  return SceneStorageStatus::Ok;
}
SceneStorageStatus fs_remove(void* ctx, SceneFileId id) {
  auto& fs = *static_cast<MemoryFs*>(ctx); ++fs.calls;
  Cell& c = fs.cell(id);
  if (!c.exists) return SceneStorageStatus::Missing;
  c.exists = false; c.bytes.clear();
  return SceneStorageStatus::Ok;
}
SceneStoragePort port_for(MemoryFs& fs) {
  SceneStoragePort p;
  p.context = &fs; p.exists = fs_exists; p.open_read = fs_open_read; p.open_write = fs_open_write;
  p.close = fs_close; p.rename = fs_rename; p.remove = fs_remove;
  return p;
}

struct SceneRig {
  std::unique_ptr<MemoryFs> fs = std::make_unique<MemoryFs>();
  std::unique_ptr<Rig> rig;
  explicit SceneRig(MaintenanceHandshake* hs = nullptr) {
    ControllerPorts ports;
    ports.storage = port_for(*fs);
    ports.maintenance = hs;
    rig = std::make_unique<Rig>(ports);
  }
  Controller& c() { return rig->c; }
  bool run_until_idle(int max_steps = 400, MaintenanceHandshake* worker = nullptr) {
    for (int i = 0; i < max_steps && c().scene_busy(); ++i) {
      if (worker != nullptr && worker->worker_poll() == WorkerAction::Quiesce) worker->worker_acknowledge_quiescence();
      rig->step(1);
    }
    return !c().scene_busy();
  }
  void replay(const char* name) {
    for (const auto& sc : kScenarios) {
      if (std::strcmp(sc.name, name) != 0) continue;
      for (std::size_t i = 0; i < sc.count; ++i) {
        const TraceStep& st = sc.steps[i];
        switch (st.kind) {
          case 'k': if (st.b) rig->key_down(static_cast<std::uint8_t>(st.a)); else rig->key_up(static_cast<std::uint8_t>(st.a)); break;
          case 'e': if (st.a) rig->enc_down(); else rig->enc_up(); break;
          case 't': rig->turn(st.a); break;
          case 'w': rig->ms += static_cast<std::uint32_t>(st.a); break;
          default: break;
        }
        rig->step(0);
      }
      return;
    }
    CHECK(false);
  }
};

const char* python_text(const char* name) {
  for (const auto& t : kSceneTexts) if (std::strcmp(t.name, name) == 0) return t.text;
  return "";
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : text) {
    if (ch == '\r') continue;
    if (ch == '\n') { out.push_back(cur); cur.clear(); } else cur += ch;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) { if (ch == sep) { out.push_back(cur); cur.clear(); } else cur += ch; }
  out.push_back(cur);
  return out;
}
bool numeric(const std::string& s, double& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = std::strtod(s.c_str(), &end);
  return end != nullptr && *end == '\0';
}
// Scene files match when every line has identical structure and numbers agree to 1e-5
// (Python writes str(float): "0.0", "100"; the codec writes shortest round-trip text).
bool same_scene(const std::string& a, const std::string& b, std::string* why = nullptr) {
  const auto la = lines_of(a), lb = lines_of(b);
  if (la.size() != lb.size()) { if (why) *why = "line count " + std::to_string(la.size()) + " vs " + std::to_string(lb.size()); return false; }
  for (std::size_t i = 0; i < la.size(); ++i) {
    const auto fa = split(la[i], ','), fb = split(lb[i], ',');
    bool ok = fa.size() == fb.size();
    for (std::size_t k = 0; ok && k < fa.size(); ++k) {
      if (k >= 2 && fa[0] != "M") {  // value fields: compare numerically, '|' separated lists too
        const auto va = split(fa[k], '|'), vb = split(fb[k], '|');
        ok = va.size() == vb.size();
        for (std::size_t j = 0; ok && j < va.size(); ++j) {
          double x, y;
          if (numeric(va[j], x) && numeric(vb[j], y)) ok = std::fabs(x - y) < 1e-5;
          else ok = va[j] == vb[j];
        }
      } else {
        ok = fa[k] == fb[k];
      }
    }
    if (!ok) { if (why) *why = "line " + std::to_string(i) + ": '" + la[i] + "' vs '" + lb[i] + "'"; return false; }
  }
  return true;
}

void save_matches_python(const char* name) {
  SceneRig s;
  s.replay(name);
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  const std::string got = s.fs->text(SceneFileKind::Scene, 0);
  std::string why;
  const bool same = same_scene(got, python_text(name), &why);
  if (!same) std::fprintf(stderr, "scene %s differs: %s\n", name, why.c_str());
  CHECK(same);
}

void load_python_scene_roundtrip() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, python_text("scene_rich"));
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.c().request_load_active());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "matrix") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), "arp_walk") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP2), "euclid") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP3), "transpose") == 0);
  CHECK(s.c().global().octave == 8 && s.c().global().key_offset == 1);
  CHECK(s.c().global().key == 5);
  const auto& m = s.c().input_state().storage.matrix;
  CHECK(m.pad == 3 && m.sevenths[3] == 1 && m.ninths[3] == 1 && m.inversions[3] == 2 && m.notes[3] == 5);
  CHECK(s.c().chain().state(0)->storage.arp_walk.step_values[1] == 8);
  CHECK(s.c().chain().state(1)->storage.euclid.pulses[2] == 7);
  CHECK(s.c().slot(kSlotOut).out_channel == 4);
  // UI is back in boot state and the next save reproduces the Python file.
  CHECK(s.c().section() == UiSection::Chain && s.c().active_chain() == kSlotIn && s.c().active_parm() == 0);
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  std::string why;
  const bool same = same_scene(s.fs->text(SceneFileKind::Scene, 0), python_text("scene_rich"), &why);
  if (!same) std::fprintf(stderr, "round trip differs: %s\n", why.c_str());
  CHECK(same);
}

void switch_scene_releases_and_resets() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 1, python_text("scene_rich"));
  s.rig->key_down(9);
  s.rig->step();
  CHECK(s.c().transport().running() && s.c().output().is_sounding(0, 60));
  CHECK(s.c().request_scene_switch(1));
  CHECK(s.c().scene_busy());
  // Input during the operation is ignored (physical state is resynced afterwards).
  s.rig->key_down(8);
  CHECK(s.c().diagnostics().events_busy == 1);
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(s.rig->cap.count(MidiKind::NoteOff, 60) == 1);          // released before the swap
  CHECK(!s.c().transport().running());
  CHECK(s.c().active_scene() == 1);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "matrix") == 0);
  CHECK(s.fs->text(SceneFileKind::ActivePointer, 0) == "2\n");
  CHECK(!s.fs->cell({SceneFileKind::Scene, 0}).bytes.empty());   // current scene saved first
  CHECK(s.c().section() == UiSection::Chain && s.c().active_chain() == kSlotIn);
  CHECK(s.c().output().held_count() == 0);
}

void same_scene_switch_is_noop() {
  SceneRig s;
  const auto calls = s.fs->calls;
  CHECK(s.c().request_scene_switch(0));
  CHECK(!s.c().scene_busy());
  CHECK(s.fs->calls == calls);
}

void copy_scene() {
  SceneRig s;
  s.replay("scene_rich");
  CHECK(s.c().request_scene_copy(4));
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(s.c().active_scene() == 4);
  CHECK(s.fs->text(SceneFileKind::ActivePointer, 0) == "5\n");
  std::string why;
  CHECK(same_scene(s.fs->text(SceneFileKind::Scene, 4), python_text("scene_rich"), &why));
  // Source switch_scene(copy_current=True): the current scene file is not rewritten.
  CHECK(!s.fs->cell({SceneFileKind::Scene, 0}).exists);
}

void reset_scene() {
  SceneRig s;
  s.replay("scene_rich");
  CHECK(s.c().request_scene_reset());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), "empty") == 0);
  CHECK(s.c().global().octave == 3 && s.c().global().key_offset == 0);
  std::string why;
  const bool same = same_scene(s.fs->text(SceneFileKind::Scene, 0), python_text("scene_default"), &why);
  if (!same) std::fprintf(stderr, "reset differs: %s\n", why.c_str());
  CHECK(same);
}

void storage_failure_keeps_state() {
  SceneRig s;
  s.replay("scene_rich");
  s.fs->fail_open_write = true;
  CHECK(s.c().request_scene_switch(2));
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::StorageError);
  CHECK(s.c().diagnostics().scene_failures == 1);
  CHECK(s.c().active_scene() == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "matrix") == 0);   // prior scene intact
}

void settings_slot_actions() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 1, python_text("scene_default"));
  Rig& r = *s.rig;
  r.turn(-2); r.step();                       // chain view -> SETTINGS (slot 9)
  CHECK(r.c.active_chain() == kSlotSettings);
  r.enc_down(); r.enc_up(); r.step();         // parameter selection
  CHECK(r.c.section() == UiSection::ParmSelection && r.c.active_parm() == 0);
  r.enc_down(); r.enc_up(); r.step();         // edit SCENE
  r.turn(1); r.step();
  CHECK(!s.c().scene_busy());                 // nothing until the exit callback
  r.enc_down(); r.enc_up(); r.step();         // exit -> switch_scene(1)
  CHECK(s.c().scene_busy());
  CHECK(s.run_until_idle());
  CHECK(s.c().active_scene() == 1);
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  // SAVSCN: entering the button saves the active scene (UI is in boot state again).
  r.turn(-2); r.step();
  r.enc_down(); r.enc_up(); r.step();
  r.turn(1); r.step();
  CHECK(!s.fs->cell({SceneFileKind::Scene, 1}).bytes.empty());
  s.fs->cell({SceneFileKind::Scene, 1}).bytes.clear();
  r.enc_down(); r.enc_up(); r.step();         // enter SAVSCN
  CHECK(s.c().scene_busy());
  CHECK(s.run_until_idle());
  CHECK(!s.fs->cell({SceneFileKind::Scene, 1}).bytes.empty());
  CHECK(r.c.section() == UiSection::ParmSelection);   // button returns to selection
  // KEYBRI: brightness edit flags the user-settings file for the storage owner.
  r.turn(4); r.step();
  r.enc_down(); r.enc_up(); r.step();
  r.turn(-4); r.step();
  r.enc_down(); r.enc_up(); r.step();
  CHECK(std::fabs(r.c.key_brightness() - 0.8f) < 1e-6f);
  // T14: leaving the KEYBRI edit saves while stopped; the save consumes the dirty flag.
  CHECK(s.run_until_idle());
  CHECK(!r.c.take_user_settings_dirty());
  CHECK(r.c.diagnostics().settings_saves == 1);
  CHECK(!s.fs->cell({SceneFileKind::UserSettings, 0}).bytes.empty());
}

void quiesce_handshake() {
  MaintenanceHandshake hs;
  SceneRig s(&hs);
  CHECK(s.c().request_scene_save());
  for (int i = 0; i < 20; ++i) s.rig->step(1);
  CHECK(s.c().scene_busy());
  CHECK(s.fs->writes == 0);                    // no flash write before the worker acknowledges
  CHECK(s.run_until_idle(100, &hs));
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(s.fs->writes > 0);
  // No acknowledgement: bounded timeout, state untouched, controller usable again.
  MaintenanceHandshake hs2;
  SceneRig t(&hs2);
  CHECK(t.c().request_scene_save());
  for (int i = 0; i < 30; ++i) t.rig->step(100);
  CHECK(!t.c().scene_busy());
  CHECK(t.c().last_scene_status() == SceneServiceStatus::QuiesceTimeout);
  CHECK(t.fs->writes == 0);
}

void every_module_schema_round_trips() {
  const char* processors[] = {"empty", "arp", "arp_walk", "bounce", "chord", "chopper", "delay", "drop", "euclid",
                              "gate", "latch", "pick", "quantize", "randomize", "retrigger", "strum", "transpose",
                              "wander"};
  const char* inputs[] = {"note", "chords", "drum", "matrix", "note_walk", "chord_walk"};
  for (const char* in : inputs) {
    SceneRig s;
    std::string text = std::string("SCN1\nM,1,") + in + ",1\nEND\n";
    s.fs->put(SceneFileKind::Scene, 0, text);
    s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
    CHECK(s.c().request_load_active());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
    CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), in) == 0);
    CHECK(s.c().request_scene_save());
    CHECK(s.run_until_idle());
    if (s.c().last_scene_status() != SceneServiceStatus::Succeeded) std::fprintf(stderr, "input %s save failed\n", in);
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  }
  for (const char* id : processors) {
    SceneRig s;
    std::string text = std::string("SCN1\nM,2,") + id + ",1\nEND\n";
    s.fs->put(SceneFileKind::Scene, 0, text);
    s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
    CHECK(s.c().request_load_active());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
    CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), id) == 0);
    CHECK(s.c().request_scene_save());
    CHECK(s.run_until_idle());
    if (s.c().last_scene_status() != SceneServiceStatus::Succeeded) std::fprintf(stderr, "module %s save failed\n", id);
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  }
}

void unknown_module_falls_back() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 0, "SCN1\nM,1,phraser,1\nM,2,phraser,1\nEND\n");
  s.fs->put(SceneFileKind::ActivePointer, 0, "1\n");
  CHECK(s.c().request_load_active());
  CHECK(s.run_until_idle());
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  CHECK(std::strcmp(s.c().slot_module_id(kSlotP1), "empty") == 0);
}

void epoch_advances_after_scene_operation() {
  SceneRig s;
  s.fs->put(SceneFileKind::Scene, 1, python_text("scene_default"));
  PhysicalEventBridge bridge;
  CHECK(s.c().request_scene_switch(1));
  for (int i = 0; i < 200 && s.c().scene_busy(); ++i) { s.rig->ms += 1; (void)s.c().poll(bridge, s.rig->args()); }
  CHECK(!s.c().scene_busy());
  s.rig->ms += 1;
  (void)s.c().poll(bridge, s.rig->args());
  CHECK(s.c().epoch() == 1);
  CHECK(bridge.awaiting_resync());                       // old physical state is not trusted
  PhysicalEvent e{};
  e.kind = InputKind::KeyEdge; e.key = 9; e.value = 1; e.at_ms = s.rig->ms;
  (void)bridge.publish(e, 1U << 9);
  s.rig->ms += 1;
  (void)s.c().poll(bridge, s.rig->args());
  CHECK(s.rig->cap.count(MidiKind::NoteOn) == 0);        // gated until an all-up resync
  PhysicalEvent resync{};
  resync.kind = InputKind::Resync; resync.at_ms = s.rig->ms;
  (void)bridge.publish(resync, 0);
  s.rig->ms += 1;
  (void)s.c().poll(bridge, s.rig->args());
  e.value = 1;
  (void)bridge.publish(e, 1U << 9);
  s.rig->ms += 1;
  (void)s.c().poll(bridge, s.rig->args());
  CHECK(s.rig->cap.count(MidiKind::NoteOn, 60) == 1);
}

// Regression for the C++ nav-note mirror (intentional deviation from Python, scene-affecting
// only): octave/key_offset changed with note navigation mirror into the IN parameter rows even
// while another slot is the active chain. A scene saved afterwards must round-trip them.
void octave_offset_round_trip_with_other_slot_active() {
  SceneRig s;
  s.rig->enc_down(); s.rig->step();
  s.rig->chord_key(6);                       // shortcut: note navigation -> parameter navigation
  CHECK(s.c().nav_keys_state() == 1);
  s.rig->chord_key(11);                      // parameter nav right: active chain moves off IN
  CHECK(s.c().active_chain() != kSlotIn);
  s.rig->chord_key(6);                       // back to note navigation
  CHECK(s.c().nav_keys_state() == 0);
  const std::int32_t octave0 = s.c().global().octave;
  const std::int32_t offset0 = s.c().global().key_offset;
  s.rig->chord_key(7);                       // note nav up: octave +1
  s.rig->chord_key(11);                      // note nav right: key_offset +1
  s.rig->chord_key(11);                      // key_offset +1
  const std::int32_t octave = s.c().global().octave;
  const std::int32_t offset = s.c().global().key_offset;
  CHECK(octave == octave0 + 1 && offset == offset0 + 2);
  const ParamRow* oct_row = row(s.c(), kSlotIn, "octave");
  const ParamRow* off_row = row(s.c(), kSlotIn, "key_offset");
  CHECK(oct_row != nullptr && oct_row->value_i == octave);   // IN rows mirror while another chain is active
  CHECK(off_row != nullptr && off_row->value_i == offset);
  s.rig->enc_up(); s.rig->step();
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);

  // Change the live values, then load the saved scene: the saved values must come back.
  s.rig->enc_down(); s.rig->step();
  s.rig->chord_key(7);
  s.rig->enc_up(); s.rig->step();
  CHECK(s.c().global().octave == octave + 1);
  CHECK(s.c().request_load_active());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(s.c().global().octave == octave);
  CHECK(s.c().global().key_offset == offset);
}

}  // namespace

int main() {
  octave_offset_round_trip_with_other_slot_active();
  save_matches_python("scene_default");
  save_matches_python("scene_rich");
  load_python_scene_roundtrip();
  switch_scene_releases_and_resets();
  same_scene_switch_is_noop();
  copy_scene();
  reset_scene();
  storage_failure_keeps_state();
  settings_slot_actions();
  quiesce_handshake();
  every_module_schema_round_trips();
  unknown_module_falls_back();
  epoch_advances_after_scene_operation();
  if (g_failures == 0) std::printf("t11b_scenes: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
