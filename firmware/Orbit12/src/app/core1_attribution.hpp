#pragma once

// Core 1 loop attribution (diagnostics only, host-compilable, no heap).
//
// Every core-1 iteration is split into contiguous phases by stamping the clock at
// each stage boundary, so the phases sum to the iteration body exactly. The time
// between the end of one body and the start of the next (the "gap") is outside any
// code this worker runs: an interrupt, a flash lockout (idleOtherCore), preemption
// or the loop wrapper. A worst-case record keeps the phase breakdown of the slow
// body together with its gap, so the dump can say which bucket dominated.
//
// Period semantics: period(k) = start(k) - start(k-1) = body(k-1) + gap(k).

#include <cstddef>
#include <cstdint>

namespace orbit {

enum class Core1Phase : std::uint8_t {
  Scan,     // GPIO/encoder scan, bridge emits, resync
  Power,    // screen sleep/wake bookkeeping and panel power commands
  Model,    // core-0 snapshot and diagnostics drains, dirty-pixel diff
  Pixels,   // NeoPixel show (timed in C1_PIXELS_US)
  Compose,  // frame composition from the snapshot (C1_COMPOSE_US)
  Present,  // SH1106 frame transfer (C1_RENDER_US)
  Tail,     // stack watermark and statistics publication
  Count
};

constexpr std::size_t kCore1PhaseCount = static_cast<std::size_t>(Core1Phase::Count);

inline constexpr const char* const kCore1PhaseNames[kCore1PhaseCount] = {
    "scan", "power", "model", "pixels", "compose", "present", "tail"};

// Uptime at which a worst case counts as steady state (boot, splash and first
// scene load are excluded from the steady record).
constexpr std::uint32_t kCore1SteadyAfterMs = 5000;
constexpr std::uint32_t kCore1SlowPeriodUs = 4000;
constexpr std::uint32_t kCore1SlowGapUs = 2000;

// Per-iteration phase accumulator. begin() at the start of the body, stage() at
// each boundary with a timestamp taken there. Wrap-safe with uint32 microseconds.
struct Core1Iteration {
  std::uint32_t mark_us = 0;
  std::uint32_t phase[kCore1PhaseCount]{};

  void begin(std::uint32_t now_us) noexcept {
    mark_us = now_us;
    for (std::uint32_t& value : phase) value = 0;
  }
  void stage(Core1Phase which, std::uint32_t now_us) noexcept {
    phase[static_cast<std::size_t>(which)] += now_us - mark_us;
    mark_us = now_us;
  }
};

// The slowest body seen in a window, with its gap and phase breakdown.
struct Core1Worst {
  std::uint32_t period_us = 0;  // body(previous) + gap(this)
  std::uint32_t gap_us = 0;     // outside-body time before this iteration
  std::uint32_t at_ms = 0;      // uptime when the slow period was observed
  std::uint32_t phase[kCore1PhaseCount]{};  // phases of the previous body

  // Keeps the record only when the period is strictly larger (first worst wins ties).
  bool observe(std::uint32_t period, std::uint32_t gap, std::uint32_t now_ms,
               const std::uint32_t* phases) noexcept {
    if (period <= period_us) return false;
    period_us = period;
    gap_us = gap;
    at_ms = now_ms;
    for (std::size_t i = 0; i < kCore1PhaseCount; ++i) phase[i] = phases[i];
    return true;
  }
};

// The largest contributor of a worst record: "gap" (outside any measured code) or
// one of the phase names. Ties resolve toward "gap", then the earlier phase.
inline const char* core1_top_contributor(const Core1Worst& worst) noexcept {
  if (worst.period_us == 0) return "none";
  std::uint32_t best = worst.gap_us;
  const char* name = "gap";
  for (std::size_t i = 0; i < kCore1PhaseCount; ++i) {
    if (worst.phase[i] > best) {
      best = worst.phase[i];
      name = kCore1PhaseNames[i];
    }
  }
  return name;
}

// Whole-run and steady-state (after kCore1SteadyAfterMs) attribution, plus slow counters.
struct Core1Attribution {
  Core1Worst all;
  Core1Worst steady;
  std::uint32_t slow_periods = 0;        // period > kCore1SlowPeriodUs, whole run
  std::uint32_t slow_gaps = 0;           // gap > kCore1SlowGapUs, whole run
  std::uint32_t steady_slow_periods = 0; // period > kCore1SlowPeriodUs after steady start

  // Called once per observed iteration with the previous body's phases.
  void note(std::uint32_t period, std::uint32_t gap, std::uint32_t now_ms,
            const std::uint32_t* previous_phases) noexcept {
    const bool is_steady = now_ms >= kCore1SteadyAfterMs;
    all.observe(period, gap, now_ms, previous_phases);
    if (is_steady) steady.observe(period, gap, now_ms, previous_phases);
    if (period > kCore1SlowPeriodUs) {
      ++slow_periods;
      if (is_steady) ++steady_slow_periods;
    }
    if (gap > kCore1SlowGapUs) ++slow_gaps;
  }

};

// Dump line: NAME period=.. gap=.. at_ms=.. top=.. then one field per phase.
// Templated on the text sink so the host tests can check the exact format.
template <typename Out>
void write_core1_worst(Out& out, const char* name, const Core1Worst& worst) {
  out.s(name).s(" period=").u(worst.period_us).s(" gap=").u(worst.gap_us)
      .s(" at_ms=").u(worst.at_ms).s(" top=").s(core1_top_contributor(worst));
  for (std::size_t i = 0; i < kCore1PhaseCount; ++i) {
    out.s(" ").s(kCore1PhaseNames[i]).s("=").u(worst.phase[i]);
  }
  out.nl();
}

}  // namespace orbit
