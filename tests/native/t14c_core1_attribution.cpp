// T14c core-1 loop attribution (app/core1_attribution.hpp): phase bookkeeping is
// contiguous and wrap-safe, worst records keep the slow body's breakdown, the top
// contributor classifies gap versus phases, the steady record excludes boot time,
// and the dump line format is exact. Host-only; no firmware code is linked.
#include <cstdint>
#include <cstdio>
#include <string>

#include "app/core1_attribution.hpp"

static int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

using namespace orbit;

namespace {

// Text sink with the TextOut surface used by write_core1_worst.
struct Sink {
  std::string text;
  Sink& s(const char* value) { text += value; return *this; }
  Sink& u(std::uint32_t value) { text += std::to_string(value); return *this; }
  Sink& nl() { text += '\n'; return *this; }
};

constexpr std::size_t idx(Core1Phase p) { return static_cast<std::size_t>(p); }

void phases_are_contiguous_and_wrap_safe() {
  // A body that starts just before the 32-bit microsecond counter wraps.
  Core1Iteration it;
  const std::uint32_t start = 0xFFFFFFF0U;
  it.begin(start);
  it.stage(Core1Phase::Scan, start + 5U);
  it.stage(Core1Phase::Power, start + 5U);          // zero-length phase
  it.stage(Core1Phase::Model, start + 30U);         // crosses the wrap
  it.stage(Core1Phase::Tail, start + 40U);
  std::uint32_t sum = 0;
  for (std::uint32_t value : it.phase) sum += value;
  CHECK(sum == 40U);                                 // phases sum to the body exactly
  CHECK(it.phase[idx(Core1Phase::Scan)] == 5U);
  CHECK(it.phase[idx(Core1Phase::Power)] == 0U);
  CHECK(it.phase[idx(Core1Phase::Model)] == 25U);
  CHECK(it.phase[idx(Core1Phase::Compose)] == 0U);

  it.begin(1000U);                                   // begin clears the previous body
  sum = 0;
  for (std::uint32_t value : it.phase) sum += value;
  CHECK(sum == 0U);
  CHECK(it.mark_us == 1000U);
}

void worst_keeps_largest_period_and_its_breakdown() {
  Core1Worst worst;
  const std::uint32_t first[kCore1PhaseCount] = {1, 0, 2, 0, 0, 0, 3};
  CHECK(worst.observe(100, 10, 7000, first));
  const std::uint32_t smaller[kCore1PhaseCount] = {9, 9, 9, 9, 9, 9, 9};
  CHECK(!worst.observe(100, 50, 8000, smaller));    // ties keep the first record
  CHECK(!worst.observe(90, 50, 8000, smaller));
  CHECK(worst.period_us == 100U && worst.gap_us == 10U && worst.at_ms == 7000U);
  CHECK(worst.phase[idx(Core1Phase::Scan)] == 1U && worst.phase[idx(Core1Phase::Model)] == 2U);
  const std::uint32_t larger[kCore1PhaseCount] = {0, 0, 0, 0, 40, 0, 0};
  CHECK(worst.observe(500, 300, 9000, larger));
  CHECK(worst.period_us == 500U && worst.gap_us == 300U && worst.phase[idx(Core1Phase::Compose)] == 40U);
  CHECK(worst.phase[idx(Core1Phase::Scan)] == 0U);  // the snapshot replaces, never merges
}

void top_contributor_classification() {
  Core1Worst none;
  CHECK(std::string(core1_top_contributor(none)) == "none");

  Core1Worst outside;
  const std::uint32_t small[kCore1PhaseCount] = {10, 0, 20, 0, 30, 40, 5};
  outside.observe(9800, 9000, 1, small);
  CHECK(std::string(core1_top_contributor(outside)) == "gap");  // outside code dominates

  Core1Worst inside;
  const std::uint32_t composed[kCore1PhaseCount] = {10, 0, 20, 0, 7000, 1500, 5};
  inside.observe(9800, 1000, 1, composed);
  CHECK(std::string(core1_top_contributor(inside)) == "compose");

  Core1Worst present;
  const std::uint32_t displayed[kCore1PhaseCount] = {0, 0, 0, 0, 0, 3200, 0};
  present.observe(3300, 100, 1, displayed);
  CHECK(std::string(core1_top_contributor(present)) == "present");

  Core1Worst tie;
  const std::uint32_t equal[kCore1PhaseCount] = {500, 0, 0, 0, 0, 0, 0};
  tie.observe(1000, 500, 1, equal);
  CHECK(std::string(core1_top_contributor(tie)) == "gap");       // ties go to the gap
}

void steady_record_excludes_boot() {
  Core1Attribution attribution;
  const std::uint32_t phases[kCore1PhaseCount] = {0, 0, 0, 0, 0, 0, 0};
  attribution.note(9600, 9000, kCore1SteadyAfterMs - 1U, phases);   // boot: all only
  CHECK(attribution.all.period_us == 9600U);
  CHECK(attribution.steady.period_us == 0U);
  CHECK(attribution.slow_periods == 1U && attribution.steady_slow_periods == 0U);
  CHECK(attribution.slow_gaps == 1U);

  attribution.note(5000, 4800, kCore1SteadyAfterMs, phases);        // steady starts here
  CHECK(attribution.steady.period_us == 5000U);
  CHECK(attribution.steady_slow_periods == 1U);
  CHECK(attribution.all.period_us == 9600U);                        // whole-run worst unchanged

  attribution.note(kCore1SlowPeriodUs, kCore1SlowGapUs, 6000, phases);  // thresholds are strict
  CHECK(attribution.slow_periods == 2U);            // 4000 us is not above the threshold
  CHECK(attribution.slow_gaps == 2U);               // 2000 us is not above the threshold
  CHECK(attribution.steady_slow_periods == 1U);
}

void worst_line_format_is_exact() {
  Core1Worst worst;
  const std::uint32_t phases[kCore1PhaseCount] = {1, 0, 2, 0, 0, 3200, 4};
  worst.observe(9843, 9000, 12345, phases);
  Sink out;
  write_core1_worst(out, "C1_WORST_ALL", worst);
  CHECK(out.text == "C1_WORST_ALL period=9843 gap=9000 at_ms=12345 top=gap "
                    "scan=1 power=0 model=2 pixels=0 compose=0 present=3200 tail=4\n");
  CHECK(kCore1PhaseNames[idx(Core1Phase::Compose)] == std::string("compose"));
}

void phase_names_follow_enum_order() {
  static_assert(kCore1PhaseCount == 7U, "phase list changed: update the dump format and tests");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Scan)]) == "scan");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Power)]) == "power");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Model)]) == "model");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Pixels)]) == "pixels");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Present)]) == "present");
  CHECK(std::string(kCore1PhaseNames[idx(Core1Phase::Tail)]) == "tail");
}

}  // namespace

int main() {
  phases_are_contiguous_and_wrap_safe();
  worst_keeps_largest_period_and_its_breakdown();
  top_contributor_classification();
  steady_record_excludes_boot();
  worst_line_format_is_exact();
  phase_names_follow_enum_order();
  if (g_failures != 0) {
    std::fprintf(stderr, "t14c_core1_attribution: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("t14c_core1_attribution: phase, worst-record, classification, steady and format checks passed\n");
  return 0;
}
