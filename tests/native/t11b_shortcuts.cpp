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
    CHECK(c.slot(c.active_chain()).row_count == ex.count);
    CHECK(static_cast<int>(c.diagnostics().release_calls - c.diagnostics().deviation_releases) == ex.notes_off);
    CHECK(slot_ids(c) == ex.slots);
    const SlotRecord& rec = c.slot(c.active_chain());
    for (int v = 0; v < ex.nvalues && v < rec.row_count; ++v) {
      const ParamRow& r = rec.rows[v];
      const double got = (r.kind == RowKind::Decimal || r.kind == RowKind::Percent) ? static_cast<double>(r.value_f)
                                                                                    : static_cast<double>(r.value_i);
      CHECK(std::fabs(got - ex.values[v]) < 1e-5);
    }
    if (g_failures != before) {
      std::fprintf(stderr, "  release_calls=%u deviation=%u expected=%d\n", c.diagnostics().release_calls,
                   c.diagnostics().deviation_releases, ex.notes_off);
      std::fprintf(stderr, "  scenario %s failed at pass %zu (step %c %d %d)\n", sc.name, i, st.kind, st.a, st.b);
      return;  // later passes would only repeat the divergence
    }
  }
}

void differential_traces() {
  for (const auto& sc : kScenarios) replay(sc);
}
}  // namespace

int main() {
  differential_traces();
  if (g_failures == 0) std::printf("t11b_shortcuts: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
