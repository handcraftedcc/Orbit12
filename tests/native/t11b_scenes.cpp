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
// All SceneFileKind values (user settings and scene names included) and every named slot.
constexpr std::size_t kKinds = static_cast<std::size_t>(SceneFileKind::SceneNamesBackup) + 1, kScenes = kSceneSlotCount;
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
    // Named slots 1..10 (the scene tests address slots 0..9), loaded as boot does.
    std::string names;
    for (int slot = 1; slot <= 10; ++slot) {
      char line[16];
      std::snprintf(line, sizeof(line), "%02d,S%02d\n", slot, slot);
      names += line;
    }
    fs->put(SceneFileKind::SceneNames, 0, names);
    // A handshake-gated rig is not booted here: its worker acknowledges quiesce only when
    // the test drives it, and the tests that use it do not depend on names.
    if (hs == nullptr) {
      (void)rig->c.scene_service().request(SceneOperation::LoadNames);
      run_until_idle();
    }
  }
  Controller& c() { return rig->c; }
  // Boots the names (LoadNames) for a handshake-gated rig, acknowledging quiesce through
  // `worker`. Saves are refused until this succeeds (names untrusted before the boot read).
  bool boot_names(MaintenanceHandshake* worker) {
    const bool requested = rig->c.scene_service().request(SceneOperation::LoadNames);
    const bool idle = run_until_idle(400, worker);
    fs->writes = 0;  // the boot read is not a write; the quiesce tests count writes from here
    return requested && idle && c().scene_service().names_loaded();
  }
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

// Scene text without the input bank sections (BANK/BP/BA lines). The Python oracle has none;
// replays that switch input modules now save their parked banks, checked by the bank tests.
std::string without_banks(const std::string& text) {
  std::string out;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    end = end == std::string::npos ? text.size() : end + 1;
    const std::string line = text.substr(start, end - start);
    const bool bank = line.rfind("BANK,", 0) == 0 || line.rfind("BP,", 0) == 0 || line.rfind("BA,", 0) == 0;
    if (!bank) out += line;
    start = end;
  }
  return out;
}

void save_matches_python(const char* name) {
  SceneRig s;
  s.replay(name);
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  const std::string got = without_banks(s.fs->text(SceneFileKind::Scene, 0));
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
  CHECK(same_scene(without_banks(s.fs->text(SceneFileKind::Scene, 4)), python_text("scene_rich"), &why));
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
  r.turn(4); r.step();   // SAVSCN -> KEYBRI (rows: SCENE, SAVSCN, RNMSCN, CPYSCN, RSTSCN, KEYBRI)
  r.enc_down(); r.enc_up(); r.step();
  r.turn(-4); r.step();   // edit KEYBRI 1.0 -> 0.8
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
  CHECK(s.boot_names(&hs));
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
  CHECK(t.boot_names(&hs2));
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
  // The boot names load also advances the epoch; settle it before taking the baseline.
  for (int i = 0; i < 3; ++i) s.rig->step(1);
  (void)s.c().poll(bridge, s.rig->args());
  const std::uint32_t epoch_before = s.c().epoch();
  CHECK(s.c().request_scene_switch(1));
  for (int i = 0; i < 200 && s.c().scene_busy(); ++i) { s.rig->ms += 1; (void)s.c().poll(bridge, s.rig->args()); }
  CHECK(!s.c().scene_busy());
  s.rig->ms += 1;
  (void)s.c().poll(bridge, s.rig->args());
  CHECK(s.c().epoch() == epoch_before + 1);
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

// ---- Scene name editor (SCENE row [NEW] entry, SCENE list scroll, footer previews) ----
namespace {

std::string footer_text(const UiModel& m) {
  return m.footer_text_active ? std::string(m.footer_text) : std::string();
}

// Settings page, parameter selection on the SCENE row (the chain starts on IN).
void go_to_scene_row(Rig& r) {
  r.turn(-2); r.step();                 // chain view -> SETTINGS (slot 9)
  r.enc_down(); r.enc_up(); r.step();  // parameter selection, SCENE row
}

// Enters the SCENE row's edit (ParmEdit) with the encoder.
void enter_scene_edit(Rig& r) {
  r.enc_down(); r.enc_up(); r.step();
}

// Name editor: turns the ring to `target` and presses the encoder (accepts the item).
void ring_press(Rig& r, std::uint8_t target) {
  const int current = r.model().name_ring;
  if (current != target) { r.turn(target - current); r.step(); }
  r.enc_down(); r.enc_up(); r.step();
}

// Saves 1..10 are the names of SceneRig; ALFA is the first free NATO word.
void open_editor_from_scene_row(Rig& r) {
  go_to_scene_row(r);
  enter_scene_edit(r);
  r.turn(10); r.step();                 // [NEW]
  r.enc_down(); r.enc_up(); r.step();  // exit ParmEdit on [NEW]: editor opens
}

void scene_row_scroll_and_new_entry() {
  SceneRig s;
  Rig& r = *s.rig;
  go_to_scene_row(r);
  CHECK(footer_text(r.model()) == "S01");  // highlighted SCENE row: the active name
  enter_scene_edit(r);
  CHECK(r.c.section() == UiSection::ParmEdit);
  CHECK(r.model().rows[0].kind == RowKind::SceneSelect);
  CHECK(r.model().rows[0].ivalue == 0);
  CHECK(footer_text(r.model()) == "S01");
  r.turn(1); r.step();
  CHECK(r.model().rows[0].ivalue == 1);
  CHECK(footer_text(r.model()) == "S02");
  r.turn(9); r.step();                  // index 10: [NEW]
  CHECK(r.model().rows[0].ivalue == kSceneListNewValue);
  CHECK(footer_text(r.model()) == "NEW");
  r.turn(1); r.step();                  // wraps to the first name
  CHECK(footer_text(r.model()) == "S01");
  r.turn(-1); r.step();                 // wraps back to [NEW]
  CHECK(footer_text(r.model()) == "NEW");
  r.turn(-1); r.step();                 // S10
  CHECK(footer_text(r.model()) == "S10");
  r.enc_down(); r.enc_up(); r.step();  // release on a name switches to it
  CHECK(s.run_until_idle());
  CHECK(s.c().active_scene() == 9);
  CHECK(r.c.section() == UiSection::Chain);
}

void new_scene_confirm_accepts_suggestion() {
  SceneRig s;
  Rig& r = *s.rig;
  open_editor_from_scene_row(r);
  CHECK(r.c.section() == UiSection::NameEdit);
  CHECK(std::strcmp(r.model().name_text, "ALFA") == 0);
  CHECK(r.model().name_length == 4);
  CHECK(r.model().name_ring == kNameRingConfirm);  // one [C] press accepts the suggestion
  CHECK(footer_text(r.model()) == "CONFIRM");  // editor: the selected control (ring on [C])
  // Ring order: ... 9, DELETE, CONFIRM, CANCEL, A ... (wrapping both ways).
  r.turn(-1); r.step();
  CHECK(r.model().name_ring == kNameRingDelete);
  r.turn(-1); r.step();
  CHECK(r.model().name_ring == 35);      // digit 9
  r.turn(2); r.step();
  CHECK(r.model().name_ring == kNameRingConfirm);
  r.enc_down(); r.enc_up(); r.step();  // [C]
  CHECK(s.c().scene_busy());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(s.c().scene_service().occupied_count() == 11);
  CHECK(std::strcmp(s.c().scene_service().slot_name(10), "ALFA") == 0);
  CHECK(s.c().active_scene() == 10);
  CHECK(r.c.section() == UiSection::Chain);  // the create rebuilds the UI, as a switch does
}

void new_scene_cancel_restores_scene_row() {
  SceneRig s;
  Rig& r = *s.rig;
  open_editor_from_scene_row(r);
  ring_press(r, kNameRingCancel);
  CHECK(r.c.section() == UiSection::ParmSelection);
  CHECK(r.c.active_chain() == kSlotSettings && r.c.active_parm() == 0);
  CHECK(r.model().name_length == 0);
  CHECK(s.c().scene_service().occupied_count() == 10 && s.c().active_scene() == 0);
  CHECK(!s.c().scene_busy());
  CHECK(s.fs->writes == 0);               // nothing written
  CHECK(r.model().rows[0].ivalue == 0);   // the SCENE row shows the active slot again
  CHECK(footer_text(r.model()) == "S01");  // cancelled: the active name
}

void new_scene_name_editing_rules() {
  SceneRig s;
  Rig& r = *s.rig;
  open_editor_from_scene_row(r);
  for (int i = 0; i < 4; ++i) ring_press(r, kNameRingDelete);  // [<] removes the suggestion
  CHECK(r.model().name_length == 0);
  ring_press(r, kNameRingConfirm);       // an empty name is refused and stays in the editor
  CHECK(r.c.section() == UiSection::NameEdit);
  CHECK(r.model().name_message == NameEditMessage::Invalid);
  CHECK(footer_text(r.model()) == "NAME INVALID");
  CHECK(!s.c().scene_busy() && s.c().scene_service().occupied_count() == 10);
  // A saved name is refused as a duplicate (S01 is slot 0).
  ring_press(r, 18); ring_press(r, 26); ring_press(r, 27);  // S, 0, 1
  CHECK(std::strcmp(r.model().name_text, "S01") == 0);
  ring_press(r, kNameRingConfirm);
  CHECK(r.c.section() == UiSection::NameEdit);
  CHECK(r.model().name_message == NameEditMessage::Taken);
  CHECK(footer_text(r.model()) == "NAME TAKEN");
  CHECK(!s.c().scene_busy() && s.c().scene_service().occupied_count() == 10);
  // Undo the last character, then type a free name; an edit clears the message.
  ring_press(r, kNameRingDelete);       // "S0"
  CHECK(std::strcmp(r.model().name_text, "S0") == 0);
  CHECK(footer_text(r.model()) == "DELETE");  // ring on [<]
  ring_press(r, 25);                    // Z -> "S0Z"
  for (int i = 0; i < 5; ++i) ring_press(r, 0);  // A x5 -> 8 characters, the ring rests on [C]
  CHECK(r.model().name_length == 8);
  CHECK(r.model().name_ring == kNameRingConfirm);
  r.turn(1); r.step();                  // full: letters are skipped, the ring stays on controls
  CHECK(r.model().name_ring == kNameRingCancel);
  r.turn(-1); r.step();
  CHECK(r.model().name_ring == kNameRingConfirm);
  CHECK(std::strcmp(r.model().name_text, "S0ZAAAAA") == 0);
  ring_press(r, kNameRingConfirm);
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(s.c().scene_service().slot_name(10), "S0ZAAAAA") == 0);
}

void new_scene_refused_without_free_slot() {
  SceneRig s;
  Rig& r = *s.rig;
  std::string names;
  for (int slot = 1; slot <= 16; ++slot) {
    char line[16];
    std::snprintf(line, sizeof(line), "%02d,S%02d\n", slot, slot);
    names += line;
  }
  s.fs->put(SceneFileKind::SceneNames, 0, names);
  CHECK(s.boot_names(nullptr));
  go_to_scene_row(r);
  enter_scene_edit(r);
  r.turn(16); r.step();                 // [NEW] is index 16 of 17
  CHECK(r.model().rows[0].ivalue == kSceneListNewValue);
  r.enc_down(); r.enc_up(); r.step();
  CHECK(r.c.section() == UiSection::NameEdit);
  CHECK(r.model().name_length == 0);    // no free word to suggest
  ring_press(r, kNameRingConfirm);
  CHECK(r.model().name_message == NameEditMessage::NoFreeSlot);
  CHECK(footer_text(r.model()) == "NO FREE SLOT");
  CHECK(!s.c().scene_busy() && s.c().scene_service().occupied_count() == 16);
}

void new_scene_and_save_refused_without_names() {
  MaintenanceHandshake hs;
  SceneRig s(&hs);                      // names not read (no boot)
  Rig& r = *s.rig;
  go_to_scene_row(r);
  CHECK(r.model().rows[0].ivalue == kSceneListNewValue);  // only [NEW] is listed
  CHECK(!r.model().footer_text_active);  // names not read: the footer keeps "SCN NN"
  enter_scene_edit(r);
  r.enc_down(); r.enc_up(); r.step();  // exit on [NEW] (the only entry): editor opens
  CHECK(r.c.section() == UiSection::NameEdit);
  ring_press(r, kNameRingConfirm);
  CHECK(r.model().name_message == NameEditMessage::NamesUnavailable);
  CHECK(footer_text(r.model()) == "NAMES NOT LOADED");
  CHECK(!s.c().scene_busy());
  ring_press(r, kNameRingCancel);
  r.turn(1); r.step();                  // SAVSCN row
  CHECK(r.c.active_parm() == 1);
  r.enc_down(); r.enc_up(); r.step();  // SAVSCN refused: nothing written, reported as a failure
  CHECK(!s.c().scene_busy());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::NamesUnavailable);
  CHECK(s.fs->writes == 0);
}

void scene_editor_notes_and_stop() {
  SceneRig s;
  Rig& r = *s.rig;
  go_to_scene_row(r);
  r.key_down(2); r.step();              // a note starts before the editor opens
  CHECK(r.cap.count(MidiKind::NoteOn) >= 1);
  enter_scene_edit(r);
  r.turn(10); r.step();
  r.enc_down(); r.enc_up(); r.step();  // editor, note still held
  CHECK(r.c.section() == UiSection::NameEdit);
  const std::size_t on_before = r.cap.count(MidiKind::NoteOn);
  r.key_down(3); r.step(); r.key_up(3); r.step();  // keys in the editor start no note
  CHECK(r.cap.count(MidiKind::NoteOn) == on_before);
  r.key_up(2); r.step();                // the held note is released
  CHECK(r.cap.count(MidiKind::NoteOff) == r.cap.count(MidiKind::NoteOn));
  // Stop (encoder held + KEY1) still works; the ring press is consumed, not accepted.
  CHECK(r.c.transport().running());
  r.enc_down(); r.step();
  r.key_down(0); r.step(); r.key_up(0); r.step();
  r.enc_up(); r.step();
  CHECK(!r.c.transport().running());
  CHECK(r.c.section() == UiSection::NameEdit);
  CHECK(std::strcmp(r.model().name_text, "ALFA") == 0);
  CHECK(r.cap.count(MidiKind::NoteOff) == r.cap.count(MidiKind::NoteOn));
}

void scene_editor_ignores_input_while_busy() {
  SceneRig s;
  Rig& r = *s.rig;
  open_editor_from_scene_row(r);
  r.enc_down(); r.enc_up(); r.step();  // [C]: create accepted
  CHECK(s.c().scene_busy());
  const std::uint32_t busy_before = s.c().diagnostics().events_busy;
  r.turn(3); r.step();
  r.enc_down(); r.enc_up(); r.step();
  CHECK(s.c().diagnostics().events_busy >= busy_before + 3);
  CHECK(s.run_until_idle());
  CHECK(s.c().scene_service().occupied_count() == 11);
}

void pattern_footer_previews_steps() {
  SceneRig s;
  Rig& r = *s.rig;
  s.fs->put(SceneFileKind::Scene, 0, "SCN1\nM,2,arp_walk,1\nP,rhythm_pattern,3\nEND\n");
  CHECK(s.c().request_load_active());
  CHECK(s.run_until_idle());
  r.turn(1); r.step();                  // IN -> P1 (arp_walk)
  CHECK(r.c.active_chain() == kSlotP1);
  const ParamRow* pattern = r.c.find_row(kSlotP1, "rhythm_pattern");
  CHECK(pattern != nullptr);
  if (pattern == nullptr) return;
  const auto index = static_cast<int>(pattern - r.c.slot(kSlotP1).rows);
  r.enc_down(); r.enc_up(); r.step();  // parameter selection
  r.turn(index); r.step();              // to the pattern row
  CHECK(footer_text(r.model()) == "XOO");  // highlighted: steps of the current value (pattern 3)
  enter_scene_edit(r);                  // parameter edit of the pattern row (value 3, PTN4)
  CHECK(r.model().active_parm == index);
  CHECK(footer_text(r.model()) == "XOO");  // length 3, bits 100 (MSB first)
  r.turn(1); r.step();                  // pattern 4: length 4, bits 1110
  CHECK(footer_text(r.model()) == "XXXO");
  r.enc_down(); r.enc_up(); r.step();  // leave the edit, still highlighted
  CHECK(footer_text(r.model()) == "XXXO");  // selection after the edit: pattern 4 steps
  r.turn(-index); r.step();             // back to row 0 (the SCENE row)
  CHECK(footer_text(r.model()) == "S01");  // leaving the parameter restores the default
}

// Module selection footer: the highlighted module's descriptor, by registry id. IN lists the
// inputs, the processor slots list the processors (Empty included). CHRD is ambiguous as a
// label (chord processor and chords input), so the descriptor must follow the id.
void module_selection_footer() {
  SceneRig s;
  Rig& r = *s.rig;
  r.combo(3);                           // encoder held + KEY4: module selection on IN
  CHECK(r.c.section() == UiSection::ModuleSelection);
  CHECK(footer_text(r.model()) == "PADS IN SCALE");       // note
  r.turn(1); r.step();
  CHECK(footer_text(r.model()) == "CHORD ROOTS+MODS");    // chords input (label CHRD)
  r.turn(1); r.step();
  CHECK(footer_text(r.model()) == "DRUM PAD LAYOUT");     // drum
  r.enc_down(); r.enc_up(); r.step();  // apply (drum input) and leave the list
  CHECK(r.c.section() != UiSection::ModuleSelection);
  CHECK(footer_text(r.model()) == "S01");                 // default: the active name

}

// Processor list on an empty P1 (a fresh rig starts in the chain view on IN).
void module_selection_processor_footer() {
  SceneRig s;
  Rig& r = *s.rig;
  r.turn(1); r.step();                  // IN -> P1 (empty)
  CHECK(r.c.active_chain() == kSlotP1);
  r.enc_down(); r.enc_up(); r.step();  // an empty slot enters the module list directly
  CHECK(r.c.section() == UiSection::ModuleSelection);
  CHECK(footer_text(r.model()) == "NO PROCESSING");       // empty
  r.turn(1); r.step();
  CHECK(footer_text(r.model()) == "ARPEGGIATES NOTES");   // arp
  r.turn(3); r.step();                  // empty, arp, arp_walk, bounce, chord
  CHECK(footer_text(r.model()) == "STACKS INTERVALS");    // chord processor (label CHRD)
  CHECK(footer_text(r.model()) != "CHORD ROOTS+MODS");
  r.enc_down(); r.enc_up(); r.step();  // leave (chord applied to P1)
  CHECK(r.c.section() != UiSection::ModuleSelection);
  CHECK(footer_text(r.model()) == "S01");
}

void new_scene_storage_failure_keeps_names_and_notes() {
  SceneRig s;
  Rig& r = *s.rig;
  r.key_down(2); r.step();              // a note held through the create
  open_editor_from_scene_row(r);
  CHECK(r.c.section() == UiSection::NameEdit);
  s.fs->fail_open_write = true;
  ring_press(r, kNameRingConfirm);      // accepted; the storage write fails
  CHECK(s.c().scene_busy());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::StorageError);
  CHECK(s.c().scene_service().occupied_count() == 10);  // no name committed
  CHECK(s.c().active_scene() == 0);
  CHECK(r.c.section() != UiSection::NameEdit);          // the editor was left on accept
  r.key_up(2); r.step();                // the note is released, not left sounding
  CHECK(r.cap.count(MidiKind::NoteOff) == r.cap.count(MidiKind::NoteOn));
}

}  // namespace

// ---- Input banks: per-module edits kept per scene (SCN1 BANK sections) ----
namespace {

// Module selection on IN (encoder held + KEY4), move by `delta`, release applies it.
void choose_input(Rig& r, int delta) {
  r.combo(3);
  r.turn(delta);
  r.step();
  r.enc_down(); r.enc_up(); r.step();
}

// Parameter edit by name on the active input (selection, edit, release).
void edit_input_param(Rig& r, const char* name, int delta) {
  const ParamRow* row = r.c.find_row(kSlotIn, name);
  CHECK(row != nullptr);
  if (row == nullptr) return;
  const int index = static_cast<int>(row - r.c.slot(kSlotIn).rows);
  r.turn(index - r.c.active_parm()); r.step();
  r.enc_down(); r.enc_up(); r.step();
  r.turn(delta); r.step();
  r.enc_down(); r.enc_up(); r.step();
}

std::int32_t input_value(Rig& r, const char* name) {
  const ParamRow* row = r.c.find_row(kSlotIn, name);
  return row == nullptr ? -999 : row->value_i;
}

// Inserts section text before the END line of an SCN1 text.
std::string with_bank(const std::string& scene, const std::string& bank) {
  const std::size_t end = scene.rfind("END\n");
  return scene.substr(0, end) + bank + scene.substr(end);
}

void input_banks_keep_edits_per_module() {
  SceneRig s;
  Rig& r = *s.rig;
  choose_input(r, 1);                              // note -> chords
  CHECK(std::strcmp(r.c.slot_module_id(kSlotIn), "chords") == 0);
  edit_input_param(r, "spread", 2);                // chords: TIGHT -> WIDE
  CHECK(input_value(r, "spread") == 2);
  choose_input(r, -1);                             // chords -> note (parks chords)
  CHECK(std::strcmp(r.c.slot_module_id(kSlotIn), "note") == 0);
  edit_input_param(r, "velocity", -27);            // note: 127 -> 100
  CHECK(input_value(r, "velocity") == 100);
  choose_input(r, 1);                              // note -> chords (restores chords)
  CHECK(input_value(r, "spread") == 2);
  choose_input(r, -1);                             // chords -> note (restores note)
  CHECK(input_value(r, "velocity") == 100);
}

void input_bank_survives_scene_round_trip() {
  SceneRig s;
  Rig& r = *s.rig;
  choose_input(r, 1);
  edit_input_param(r, "spread", 2);
  choose_input(r, -1);                             // chords parked in the bank
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  const std::string text = s.fs->text(SceneFileKind::Scene, 0);
  CHECK(text.find("BANK,chords\n") != std::string::npos);
  CHECK(text.find("BP,spread,2\n") != std::string::npos);
  SceneRig t;                                      // a fresh controller loads the saved scene
  t.fs->put(SceneFileKind::Scene, 0, text);
  CHECK(t.c().request_load_active());
  CHECK(t.run_until_idle());
  CHECK(t.c().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(t.c().slot_module_id(kSlotIn), "note") == 0);
  choose_input(*t.rig, 1);                         // the restored bank
  CHECK(input_value(*t.rig, "spread") == 2);
}

void input_bank_matrix_attributes_round_trip() {
  SceneRig s;
  const std::string base = "SCN1\nM,1,note,1\nP,velocity,127\nEND\n";
  s.fs->put(SceneFileKind::Scene, 0,
            with_bank(base, "BANK,matrix\nBA,notes,3|0|0|0|0|0|0|0|0|0|0|0\n"));
  CHECK(s.c().request_load_active());
  CHECK(s.run_until_idle());
  CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  choose_input(*s.rig, 3);                         // note -> matrix (restores its bank)
  CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "matrix") == 0);
  CHECK(s.c().request_scene_save());
  CHECK(s.run_until_idle());
  CHECK(s.fs->text(SceneFileKind::Scene, 0).find("A,notes,3|0|0|0|0|0|0|0|0|0|0|0\n") != std::string::npos);
}

void input_bank_tolerance_policy() {
  const std::string base = "SCN1\nM,1,note,1\nP,velocity,127\nEND\n";
  {
    SceneRig s;  // unknown input id: its lines are skipped, the scene loads
    s.fs->put(SceneFileKind::Scene, 0, with_bank(base, "BANK,phraser\nBP,velocity,1\n"));
    CHECK(s.c().request_load_active());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
  }
  {
    SceneRig s;  // corrupt bank value (spread is 0..2): the scene is refused, nothing changes
    s.fs->put(SceneFileKind::Scene, 0, with_bank(base, "BANK,chords\nBP,spread,9\n"));
    CHECK(s.c().request_load_active());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() != SceneServiceStatus::Succeeded);
    CHECK(std::strcmp(s.c().slot_module_id(kSlotIn), "note") == 0);
  }
  {
    SceneRig s;  // a duplicate bank section is malformed
    s.fs->put(SceneFileKind::Scene, 0,
              with_bank(base, "BANK,chords\nBP,spread,1\nBANK,chords\nBP,spread,2\n"));
    CHECK(s.c().request_load_active());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() != SceneServiceStatus::Succeeded);
  }
}

void input_switch_releases_held_notes() {
  SceneRig s;
  Rig& r = *s.rig;
  r.key_down(2); r.step();                         // a note is sounding
  CHECK(r.cap.count(MidiKind::NoteOn) >= 1);
  choose_input(r, 1);                              // switch the input under the held key
  r.key_up(2); r.step();
  CHECK(r.cap.count(MidiKind::NoteOff) == r.cap.count(MidiKind::NoteOn));
}

void input_bank_worst_case_fits_bound() {
  SceneRig s;
  static SceneDto dto;                             // all six inputs parked, every slot full
  dto = SceneDto{};
  dto.valid = true;
  dto.banks.present = 0x3F;
  for (auto& meta : dto.banks.meta) {
    meta.parameters = 0xFFFFFFFFU;
    meta.attributes = 0xFF;
  }
  for (auto& value : dto.banks.values) value.integer = 0;
  static char text[kSceneMaxBytes];
  std::size_t written = 0;
  CHECK(encode_scene(dto, s.c().scene_schema(), text, sizeof(text), written) == SceneResult::Ok);
  CHECK(written > 0 && written <= kSceneMaxBytes);
  std::fprintf(stderr, "bank worst case: %zu of %zu bytes\n", written, kSceneMaxBytes);
}

}  // namespace

// ---- Rename the active scene (STTNGS RNMSCN) ----
namespace {

// Settings page on the RNMSCN row (SCENE, SAVSCN, RNMSCN: row 2), editor open.
void open_rename_editor(Rig& r) {
  go_to_scene_row(r);
  r.turn(2); r.step();                  // parameter selection: RNMSCN
  CHECK(r.c.active_parm() == 2);
  r.enc_down(); r.enc_up(); r.step();   // the button opens the rename editor
}

void type_name(Rig& r, const char* text) {
  for (const char* c = text; *c != '\0'; ++c) {
    const char ch = *c;
    const std::uint8_t ring = static_cast<std::uint8_t>(ch >= 'A' && ch <= 'Z' ? ch - 'A' : 26 + (ch - '0'));
    ring_press(r, ring);
  }
}

void clear_name(Rig& r) {
  while (r.model().name_length > 0) ring_press(r, kNameRingDelete);
}

void rename_flows() {
  {
    // Rename to a new name: the editor is prefilled with S01, the change writes only the names file.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    CHECK(r.c.section() == UiSection::NameEdit);
    CHECK(std::strcmp(r.model().name_text, "S01") == 0);
    CHECK(r.model().name_ring == kNameRingConfirm);
    const std::string scene_before = s.fs->text(SceneFileKind::Scene, 0);
    const std::string pointer_before = s.fs->text(SceneFileKind::ActivePointer, 0);
    clear_name(r);
    type_name(r, "MYSET");
    ring_press(r, kNameRingConfirm);
    CHECK(s.c().scene_busy());
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() == SceneServiceStatus::Succeeded);
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "MYSET") == 0);
    CHECK(r.c.section() == UiSection::ParmSelection && r.c.active_parm() == 2);
    CHECK(footer_text(r.model()) == "MYSET");              // the default footer shows the new name
    CHECK(s.fs->text(SceneFileKind::Scene, 0) == scene_before);
    CHECK(s.fs->text(SceneFileKind::ActivePointer, 0) == pointer_before);
    CHECK(s.fs->text(SceneFileKind::SceneNames, 0).find("01,MYSET\n") != std::string::npos);
  }
  {
    // The same name is a no-op: nothing is written and the editor leaves at once.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    const std::size_t writes = s.fs->writes;
    ring_press(r, kNameRingConfirm);
    CHECK(!s.c().scene_busy());
    CHECK(r.c.section() == UiSection::ParmSelection && r.c.active_parm() == 2);
    CHECK(s.fs->writes == writes);
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "S01") == 0);
  }
  {
    // A name held by another saved scene is refused; the editor stays with a message.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    clear_name(r);
    type_name(r, "S02");
    ring_press(r, kNameRingConfirm);
    CHECK(r.c.section() == UiSection::NameEdit);
    CHECK(r.model().name_message == NameEditMessage::Taken);
    CHECK(footer_text(r.model()) == "NAME TAKEN");
    CHECK(!s.c().scene_busy());
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "S01") == 0);
    CHECK(s.fs->writes == 0);
  }
  {
    // An empty name is invalid; the editor stays.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    clear_name(r);
    ring_press(r, kNameRingConfirm);
    CHECK(r.c.section() == UiSection::NameEdit);
    CHECK(footer_text(r.model()) == "NAME INVALID");
    type_name(r, "B2");                 // delete-then-confirm: the new name is typed and accepted
    ring_press(r, kNameRingConfirm);
    CHECK(s.run_until_idle());
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "B2") == 0);
  }
  {
    // CANCEL leaves with nothing changed, back to the RNMSCN row.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    clear_name(r);
    type_name(r, "XYZ");
    ring_press(r, kNameRingCancel);
    CHECK(r.c.section() == UiSection::ParmSelection && r.c.active_parm() == 2);
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "S01") == 0);
    CHECK(s.fs->writes == 0);
    CHECK(footer_text(r.model()) == "S01");
  }
  {
    // A storage failure keeps the old name in memory and the names file unchanged.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    const std::string names_before = s.fs->text(SceneFileKind::SceneNames, 0);
    s.fs->fail_open_write = true;
    clear_name(r);
    type_name(r, "LATER");
    ring_press(r, kNameRingConfirm);
    CHECK(s.run_until_idle());
    CHECK(s.c().last_scene_status() == SceneServiceStatus::StorageError);
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "S01") == 0);
    CHECK(s.fs->text(SceneFileKind::SceneNames, 0) == names_before);
    CHECK(footer_text(r.model()) == "S01");
  }
  {
    // Busy: while the rename runs, input is dropped.
    SceneRig s;
    Rig& r = *s.rig;
    open_rename_editor(r);
    clear_name(r);
    type_name(r, "BUSYNAME");
    ring_press(r, kNameRingConfirm);
    CHECK(s.c().scene_busy());
    const std::uint32_t before = s.c().diagnostics().events_busy;
    r.turn(3); r.step();
    r.enc_down(); r.enc_up(); r.step();
    CHECK(s.c().diagnostics().events_busy >= before + 3);
    CHECK(s.run_until_idle());
    CHECK(std::strcmp(s.c().scene_service().slot_name(0), "BUSYNAME") == 0);
  }
  {
    // Names not read: the rename is refused (NAMES NOT LOADED) and nothing is written.
    MaintenanceHandshake hs;
    SceneRig s(&hs);
    Rig& r = *s.rig;
    open_rename_editor(r);
    ring_press(r, kNameRingConfirm);
    CHECK(r.model().name_message == NameEditMessage::NamesUnavailable);
    CHECK(!s.c().scene_busy());
    CHECK(s.fs->writes == 0);
  }
}

}  // namespace

int main() {
  rename_flows();
  input_banks_keep_edits_per_module();
  input_bank_survives_scene_round_trip();
  input_bank_matrix_attributes_round_trip();
  input_bank_tolerance_policy();
  input_switch_releases_held_notes();
  input_bank_worst_case_fits_bound();
  scene_row_scroll_and_new_entry();
  new_scene_confirm_accepts_suggestion();
  new_scene_cancel_restores_scene_row();
  new_scene_name_editing_rules();
  new_scene_refused_without_free_slot();
  new_scene_and_save_refused_without_names();
  scene_editor_notes_and_stop();
  scene_editor_ignores_input_while_busy();
  new_scene_storage_failure_keeps_names_and_notes();
  pattern_footer_previews_steps();
  module_selection_footer();
  module_selection_processor_footer();
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
