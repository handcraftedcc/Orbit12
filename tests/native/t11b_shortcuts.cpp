// T11b: shortcut table, four-row paging, consumed-key behavior, chain swap and
// system-slot parameter flows. Expected values are differential traces produced by
// running the real CircuitPython Orion.run loop (tools/arduino/oracle/t11b_harness.py)
// with scripted inputs; see fixtures/t11b_traces.inc. Directed tests below add
// invariants the traces cannot express (release-safety lives in t11b_engine.cpp).
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

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

std::string slot_ids(const Controller& c) {
  std::string out;
  for (std::uint8_t i = 0; i < kSlotCount; ++i) {
    if (i) out += ',';
    out += c.slot(i).id;
  }
  return out;
}

void replay(const TraceScenario& sc) {
  auto rig = std::make_unique<Rig>();
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
    const TraceExpect& ex = sc.expect[i];
    const UiModel& m = rig->model();
    const Controller& c = rig->c;
    const int before = g_failures;
    CHECK(static_cast<int>(c.section()) == ex.section);
    CHECK(c.active_chain() == ex.chain);
    CHECK(c.active_parm() == ex.parm);
    CHECK(m.scroll_offset == ex.scroll);
    CHECK(c.nav_keys_state() == ex.nav);
    CHECK(c.chain_swap_mode() == ex.swap);
    CHECK(static_cast<int>(c.chain_mode()) == ex.mode);
    CHECK(c.global().octave == ex.octave);
    CHECK(c.global().key_offset == ex.offset);
    CHECK(c.transport().running() == (ex.running != 0));
    CHECK(static_cast<int>(c.global().transport_mode) == ex.tmode);
    CHECK(c.selector_index() == ex.selector);
    CHECK(c.global().key == ex.key);
    CHECK(c.global().scale == ex.scale);
    // STTNGS has the RNMSCN button after SAVSCN (deviation: the Python trace has five rows).
    const bool settings = c.active_chain() == kSlotSettings;
    CHECK(c.slot(c.active_chain()).row_count == ex.count + (settings ? 1 : 0));
    CHECK(static_cast<int>(c.diagnostics().release_calls - c.diagnostics().deviation_releases) == ex.notes_off);
    CHECK(slot_ids(c) == ex.slots);
    const SlotRecord& rec = c.slot(c.active_chain());
    for (int v = 0; v < ex.nvalues && v < rec.row_count; ++v) {
      if (settings && v == 2) continue;  // RNMSCN: a button, no trace value
      const int e = settings && v > 2 ? v - 1 : v;  // trace row index
      const ParamRow& r = rec.rows[v];
      const double got = (r.kind == RowKind::Decimal || r.kind == RowKind::Percent) ? static_cast<double>(r.value_f)
                                                                                    : static_cast<double>(r.value_i);
      CHECK(std::fabs(got - ex.values[e]) < 1e-5);
    }
    if (g_failures != before) {
      std::fprintf(stderr, "  release_calls=%u deviation=%u expected=%d\n", c.diagnostics().release_calls,
                   c.diagnostics().deviation_releases, ex.notes_off);
      std::fprintf(stderr, "  scenario %s failed at pass %zu (step %c %d %d)\n", sc.name, i, st.kind, st.a, st.b);
      return;  // later passes would only repeat the divergence
    }
  }
}


// USER-REQUESTED DEVIATION (docs/migration/known-quirks.md, "Intentional deviations"): the
// source makes key 3 jump to IN and key 6 jump to OUT. Here they cycle: key 3 goes
// IN -> SET -> IN and key 6 goes OUT -> STTNGS -> OUT; any other active slot goes to IN
// (key 3) or OUT (key 6). Section exits run first, as in the plain jump.
namespace shortcut_toggle {
constexpr std::uint8_t kKey3 = 2, kKey6 = 5, kKey4 = 3;  // physical key index = shortcut number - 1

// Encoder held while pressing and releasing a key (a shortcut), then encoder released.
void shortcut_while_held(Rig& r, std::uint8_t key) {
  r.enc_down(); r.step();
  r.key_down(key); r.step();
  r.key_up(key); r.step();
  r.enc_up(); r.step();
}

// Walks the chain view to `target` with encoder turns (chain view only).
void select_chain(Rig& r, std::uint8_t target) {
  if (r.c.section() != UiSection::Chain) shortcut_while_held(r, 4);  // key 5: chain view
  CHECK(r.c.section() == UiSection::Chain);
  const int cur = r.c.active_chain();
  int delta = static_cast<int>(target) - cur;
  if (delta > 5) delta -= 10;
  if (delta < -5) delta += 10;
  if (delta != 0) { r.turn(delta); r.step(); }
  CHECK(r.c.active_chain() == target);
}

void chain_view_cycles() {
  auto r = std::make_unique<Rig>();
  r->step();
  CHECK(r->c.active_chain() == kSlotIn);
  shortcut_while_held(*r, kKey3);                         // IN -> SET
  CHECK(r->c.active_chain() == kSlotSet && r->c.section() == UiSection::ParmSelection);
  shortcut_while_held(*r, kKey3);                         // SET -> IN
  CHECK(r->c.active_chain() == kSlotIn && r->c.section() == UiSection::ParmSelection);
  shortcut_while_held(*r, kKey3);                         // IN -> SET again
  CHECK(r->c.active_chain() == kSlotSet);
  CHECK(r->c.nav_keys_state() == 0);
}

void chain_view_from_other_slots() {
  auto r = std::make_unique<Rig>();
  r->step();
  select_chain(*r, kSlotP1);
  r->step();
  shortcut_while_held(*r, kKey3);                         // other slot -> IN
  CHECK(r->c.active_chain() == kSlotIn);
  select_chain(*r, kSlotOut);
  shortcut_while_held(*r, kKey3);                         // OUT -> IN (key 3 is not OUT's partner)
  CHECK(r->c.active_chain() == kSlotIn);
  select_chain(*r, kSlotSet);
  shortcut_while_held(*r, kKey3);                         // SET -> IN
  CHECK(r->c.active_chain() == kSlotIn);
  select_chain(*r, kSlotP2);
  shortcut_while_held(*r, kKey6);                         // other slot -> OUT
  CHECK(r->c.active_chain() == kSlotOut);
  CHECK(r->c.section() == UiSection::ParmSelection);
  shortcut_while_held(*r, kKey6);                         // OUT -> STTNGS
  CHECK(r->c.active_chain() == kSlotSettings);
  shortcut_while_held(*r, kKey6);                         // STTNGS -> OUT
  CHECK(r->c.active_chain() == kSlotOut);
}

void settings_and_out_cycles() {
  auto r = std::make_unique<Rig>();
  r->step();
  select_chain(*r, kSlotOut);
  shortcut_while_held(*r, kKey6);
  CHECK(r->c.active_chain() == kSlotSettings && r->c.section() == UiSection::ParmSelection);
  CHECK(r->c.slot(kSlotSettings).row_count == 6);          // SCENE..KEYBRI (RAM removed, RNMSCN added)
  shortcut_while_held(*r, kKey3);                          // STTNGS -> IN (key 3 is not STTNGS's partner)
  CHECK(r->c.active_chain() == kSlotIn);
}

void from_parm_edit_on_in() {
  auto r = std::make_unique<Rig>();
  r->step();
  r->enc_down(); r->enc_up(); r->step();                   // ParmSelection on IN
  r->enc_down(); r->enc_up(); r->step();                   // ParmEdit
  CHECK(r->c.section() == UiSection::ParmEdit);
  shortcut_while_held(*r, kKey3);                          // exits edit, then IN -> SET
  CHECK(r->c.section() == UiSection::ParmSelection);
  CHECK(r->c.active_chain() == kSlotSet);
}

void from_module_selection_on_in() {
  auto r = std::make_unique<Rig>();
  r->step();
  shortcut_while_held(*r, kKey4);                          // module selection on IN
  CHECK(r->c.section() == UiSection::ModuleSelection);
  shortcut_while_held(*r, kKey3);                          // exits selection, then IN -> SET
  CHECK(r->c.section() == UiSection::ParmSelection);
  CHECK(r->c.active_chain() == kSlotSet);
  CHECK(std::strcmp(r->c.slot_module_id(kSlotIn), "note") == 0);  // selection kept the module
}

void no_stuck_notes_across_toggles() {
  auto r = std::make_unique<Rig>();
  r->step();
  r->key_down(0); r->step();                               // pad 0 sounds on IN
  CHECK(r->cap.count(MidiKind::NoteOn) >= 1);
  shortcut_while_held(*r, kKey3);                          // toggle while the note is held
  shortcut_while_held(*r, kKey6);
  r->key_up(0); r->step(5);
  shortcut_while_held(*r, kKey3);
  for (int i = 0; i < 10; ++i) r->step(5);
  CHECK(r->cap.count(MidiKind::NoteOn) == r->cap.count(MidiKind::NoteOff));
}
}  // namespace shortcut_toggle

namespace shortcut_toggle {

// Maintenance gate: poll() drains only pending releases, so key events are never handled.
void ignored_during_maintenance() {
  auto r = std::make_unique<Rig>();
  r->step();
  CHECK(r->c.enter_maintenance());
  const std::uint8_t chain = r->c.active_chain();
  const UiSection section = r->c.section();
  r->bridge.publish(r->event(InputKind::EncoderSwitch, 0, 1), 0);
  r->bridge.publish(r->event(InputKind::KeyEdge, kKey3, 1), 1U << kKey3);
  r->bridge.publish(r->event(InputKind::KeyEdge, kKey6, 1), 1U << kKey6);
  for (int i = 0; i < 5; ++i) r->step();
  CHECK(r->c.active_chain() == chain && r->c.section() == section);
  CHECK(r->c.diagnostics().events_busy == 0);
  r->c.leave_maintenance();
}

}  // namespace shortcut_toggle

void differential_traces() {
  for (const auto& sc : kScenarios) replay(sc);
}
}  // namespace

int main() {
  differential_traces();
  shortcut_toggle::chain_view_cycles();
  shortcut_toggle::chain_view_from_other_slots();
  shortcut_toggle::settings_and_out_cycles();
  shortcut_toggle::from_parm_edit_on_in();
  shortcut_toggle::from_module_selection_on_in();
  shortcut_toggle::no_stuck_notes_across_toggles();
  shortcut_toggle::ignored_during_maintenance();
  if (g_failures == 0) std::printf("t11b_shortcuts: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
