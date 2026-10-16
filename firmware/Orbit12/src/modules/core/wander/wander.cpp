#include "wander.hpp"
#include "../drop/drop_randomize.hpp"
#include "../../../domain/music.hpp"
#include <cmath>
#include <cstdint>
#include <new>

// Deviations from circuitpython-source/src/apps/Orion/modules/wander.py (each is also listed in
// the t09j report):
//  W1. The transport tick is unwrapped to 64 bits from 32-bit deltas (quantize Q2). The source's
//      midi_tick is unbounded. The grid bin, the STBL step, the loop step and the seed all depend on
//      it, so the unwrap is required for the same values on every representable sequence.
//  W2. Construction cannot read transport.running (no context). was_running starts false, where the
//      source takes transport.running at construction (arp_walk 8). The difference is only a
//      transport that is already running when the module is created: the first pass then restarts
//      the grid (and resets the walk in RTRG) where the source would not.
//  W3. GATE is clamped to -100000..100000 ms and NaN is rejected. The source's FloatParmType has no
//      minmax. Release deadlines are 32-bit wrap-safe milliseconds, so a magnitude beyond the bound
//      cannot be compared. Negative values are kept and release as the source does (immediately).
//  W4. Output is all-or-nothing. A new note-on is refused, and the previous note keeps sounding,
//      when its batch cannot take both the release and the note-on. The refusal sets the batch's
//      sticky overflow and is counted (output_overflow). A pass emits at most one note-on and one
//      release, so the source's batches never overflow and this changes no reachable output.
//  W5. Stop, Panic, SceneChange and Init drop the sounding note without a release, as the source's
//      stop() does. The controller must call Output::all_notes_off on those transitions; the source's
//      stop_all_modules calls all_notes_off too. Init and SceneChange also restore the center and
//      velocity a fresh source instance would have.
// Exact, not deviations:
//  - Parameters with minmax clamp (Parm.set_value). RATE, RTHM and RTRG have no minmax and index
//    tables in the source, so out-of-range values are rejected.
//  - DEV<=0 keeps current_note at the center and adds TENS as +1 (the behavior-contracts text says
//    "forces center"; with TENS>0 the tension step still applies).
//  - The source's overshoot retry runs once and may still exceed DEV.
//  - LOOP resets current_note to the center before the rhythm-skip return.
//  - CONT and RTRG advance step_index even on skipped steps. STBL uses the grid bin.
//  - A 13th held note is not recorded and is counted (held_overflow); the source drops it silently.
//  - Seeds use seeded::random_int, exact for the source hash below 2^59.
//  - Parameter changes do not reset the walk (the source has no reset on parameter edits).

namespace orbit {
namespace {
constexpr std::uint8_t kRetriggerStable = 0;     // STBL: step = grid bin
constexpr std::uint8_t kRetriggerRetrigger = 2;  // RTRG: a note-on restarts the walk
constexpr std::uint8_t kRateIndexDefault = 8;
constexpr std::int32_t kRateIndexMax = 12;
constexpr std::int32_t kPatternMax = 24;
constexpr float kGateLimit = 100000.0f;

constexpr const char* kRateLabel = "RATE";
constexpr const char* kGravityLabel = "GRAV";
constexpr const char* kMinStepLabel = "MIN";
constexpr const char* kMaxStepLabel = "MAX";
constexpr const char* kDeviationLabel = "DEV";
constexpr const char* kTensionLabel = "TENS";
constexpr const char* kSeedLabel = "SEED";
constexpr const char* kLoopLabel = "LOOP";
constexpr const char* kRetriggerLabel = "RTRG";
constexpr const char* kPatternLabel = "RTHM";
constexpr const char* kGateLabel = "GATE";
constexpr const char* kRetriggerOptions[] = {"STBL", "CONT", "RTRG"};

// Parameter order follows the source's create_main_parms(). Names are the source parameter names.
const ParamSpec kWanderParams[] = {
    {"rate", ParamType::Integer, ParamValue::integer(kRateIndexDefault), ParamValue::integer(0), ParamValue::integer(kRateIndexMax), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
    {"gravity", ParamType::Integer, ParamValue::integer(50), ParamValue::integer(0), ParamValue::integer(100), ParamValue::integer(1), kGravityLabel, nullptr, 0, ParamPersist},
    {"min_step", ParamType::Integer, ParamValue::integer(1), ParamValue::integer(0), ParamValue::integer(16), ParamValue::integer(1), kMinStepLabel, nullptr, 0, ParamPersist},
    {"max_step", ParamType::Integer, ParamValue::integer(2), ParamValue::integer(0), ParamValue::integer(16), ParamValue::integer(1), kMaxStepLabel, nullptr, 0, ParamPersist},
    {"max_deviation", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(24), ParamValue::integer(1), kDeviationLabel, nullptr, 0, ParamPersist},
    {"tension", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(100), ParamValue::integer(1), kTensionLabel, nullptr, 0, ParamPersist},
    {"rand_seed", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(10000), ParamValue::integer(1), kSeedLabel, nullptr, 0, ParamPersist},
    {"loop_length", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(128), ParamValue::integer(1), kLoopLabel, nullptr, 0, ParamPersist},
    {"retrigger_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kRetriggerLabel, kRetriggerOptions, 3, ParamPersist},
    {"rhythm", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(kPatternMax), ParamValue::integer(1), kPatternLabel, nullptr, 0, ParamPersist},
    {"gate", ParamType::Decimal, ParamValue::decimal(100.0f), ParamValue::decimal(-kGateLimit), ParamValue::decimal(kGateLimit), ParamValue::decimal(5.0f), kGateLabel, nullptr, 0, ParamPersist},
};

void descriptor_destroy(ModuleState&) noexcept {}
const ModuleDescriptor kWanderDescriptor{"wander", 1, kWanderParams, 11, construct_wander_state,
                                         descriptor_destroy, process_wander, reset_wander_state,
                                         set_wander_param, wander_attributes};

// Source ticks compare modularly; an event is due when its deadline is strictly before now.
bool wrap_less(std::uint32_t a, std::uint32_t b) noexcept { return static_cast<std::int32_t>(a - b) < 0; }

// Floor division and modulo for a positive divisor (Python // and %).
std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  auto quotient = value / divisor;
  if (value % divisor < 0) --quotient;
  return quotient;
}

std::int64_t floor_mod(std::int64_t value, std::int64_t modulus) noexcept {
  const auto remainder = value % modulus;
  return remainder < 0 ? remainder + modulus : remainder;
}

std::int32_t magnitude(std::int32_t value) noexcept { return value < 0 ? -value : value; }

// Advances the 64-bit transport tick by the signed 32-bit difference (deviation W1).
std::int64_t unwrapped_tick(WanderModuleState& s, std::uint32_t tick) noexcept {
  if (!s.tick_known) {
    s.tick = static_cast<std::int64_t>(tick);
    s.tick_known = true;
  } else {
    s.tick += static_cast<std::int32_t>(tick - s.last_tick);
  }
  s.last_tick = tick;
  return s.tick;
}

std::int64_t rate_ticks(const WanderModuleState& s) noexcept {
  const auto requested = static_cast<std::size_t>(s.rate_index);
  const std::size_t index = requested < music::kRateMidiTicks.size() ? requested
                                                                     : static_cast<std::size_t>(kRateIndexDefault);
  return static_cast<std::int64_t>(music::kRateMidiTicks[index]);
}

// Source get_pattern_step on the selected pattern; the step is reduced modulo the length first.
bool pattern_on(const WanderModuleState& s, std::int64_t step) noexcept {
  const auto requested = static_cast<std::size_t>(s.pattern);
  const std::size_t index = requested < music::kPatternBits.size() ? requested : 0;
  const auto length = static_cast<std::int64_t>(music::kPatternLengths[index]);
  const auto position = floor_mod(step, length);
  return music::pattern_step(music::kPatternBits[index], music::kPatternLengths[index],
                             static_cast<std::uint32_t>(position)) != 0;
}

// int(gate): truncation toward zero. The bound keeps the cast in range (deviation W3).
std::uint32_t gate_ms(const WanderModuleState& s) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(s.gate));
}

bool has_pitch(const NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch == pitch) return true;
  }
  return false;
}

// NoteArray.remove_value_first: removes the oldest entry with this pitch.
void remove_first_pitch(NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch != pitch) continue;
    for (auto j = static_cast<std::uint16_t>(i + 1); j < buffer.count; ++j) buffer.items[j - 1] = buffer.items[j];
    --buffer.count;
    return;
  }
}

// Source WanderModule.update_held_notes. Returns got_note_on.
bool update_held(WanderModuleState& s, const NoteBatch& input) noexcept {
  bool got_note_on = false;
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note note = input.on.items[i];
    const bool had_notes = s.held.count > 0;
    remove_first_pitch(s.held, note.pitch);  // A re-press moves the pitch to the newest position.
    if (s.held.append(note) == PushResult::Added) {
      s.center_note = note.pitch;
      if (!had_notes) s.current_note = note.pitch;
      got_note_on = true;
    } else {
      ++s.held_overflow;  // Source: append_value fails silently; the center is not updated.
    }
    s.last_velocity = note.velocity;  // Updated even when the hold is not recorded, as in the source.
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) remove_first_pitch(s.held, input.off.items[i].pitch);
  if (s.held.count > 0 && !has_pitch(s.held, s.center_note)) {
    s.center_note = s.held.items[s.held.count - 1].pitch;
  }
  if (got_note_on && s.retrigger_mode == kRetriggerRetrigger) {
    s.step_index = 0;
    s.last_grid_bin = -1;
  }
  return got_note_on;
}

// Source reset_walk.
void reset_walk(WanderModuleState& s) noexcept {
  s.current_note = s.center_note;
  s.step_index = 0;
  s.last_grid_bin = -1;
}

// Source process_active_note_off. A refused release keeps the note marked active and sets the
// batch's sticky overflow (the chain then releases everything).
void release_active(WanderModuleState& s, std::uint32_t now_ms, bool force, NoteBatch& output) noexcept {
  if (!s.has_active) return;
  if (!force && !wrap_less(s.active_due, now_ms)) return;
  if (output.off.append(Note{s.active.pitch, 0, s.active.channel}) != PushResult::Added) {
    ++s.output_overflow;
    return;
  }
  s.has_active = false;
}

std::uint8_t transpose_current(const WanderModuleState& s, const ProcessContext& context, std::int32_t degrees) noexcept {
  return music::transpose(static_cast<std::int32_t>(s.current_note), degrees, 0, true, context.global.key,
                          context.global.scale);
}

// Source WanderModule.generate_note.
void generate_note(WanderModuleState& s, const ProcessContext& context, std::int64_t timing_step, NoteBatch& output) noexcept {
  // Stable mode follows the grid bin; CONT and RTRG follow the walk's own step counter.
  std::int64_t step = timing_step;
  if (s.retrigger_mode != kRetriggerStable) {
    step = static_cast<std::int64_t>(s.step_index);
    s.step_index += 1;
  }

  std::int64_t loop_step = step;
  if (s.loop_length > 0) {
    loop_step = floor_mod(step, s.loop_length);
    if (loop_step == 0) s.current_note = s.center_note;
  }

  // A rhythm skip changes no note and emits nothing; the sounding note keeps its gate.
  if (!pattern_on(s, loop_step)) return;

  const auto seed = static_cast<std::uint64_t>(s.random_seed_user) + static_cast<std::uint64_t>(loop_step);
  std::int32_t direction = 1;

  if (s.max_deviation <= 0) {
    s.current_note = s.center_note;
  } else {
    const auto distance = static_cast<std::int32_t>(s.current_note) - static_cast<std::int32_t>(s.center_note);
    // Gravity pulls back toward the center; the random choice handles free movement.
    if (magnitude(distance) >= s.max_deviation) {
      direction = distance > 0 ? -1 : 1;
    } else if (distance != 0 && seeded::random_int(seed, 99, 0) < s.gravity) {
      direction = distance > 0 ? -1 : 1;
    } else if (seeded::random_int(seed + 1, 1, 0) == 0) {
      direction = -1;
    }

    const std::int32_t low = s.min_step < s.max_step ? s.min_step : s.max_step;
    const std::int32_t high = s.min_step > s.max_step ? s.min_step : s.max_step;
    const auto step_size = low + static_cast<std::int32_t>(seeded::random_int(seed + 2, high - low, 0));
    auto next_note = transpose_current(s, context, direction * step_size);

    // If the first step overshoots the allowed range, try the opposite direction once.
    const auto next_distance = static_cast<std::int32_t>(next_note) - static_cast<std::int32_t>(s.center_note);
    if (magnitude(next_distance) > s.max_deviation) {
      if (distance > 0) {
        direction = -1;
      } else if (distance < 0) {
        direction = 1;
      } else {
        direction = static_cast<std::int32_t>(next_note) > static_cast<std::int32_t>(s.center_note) ? -1 : 1;
      }
      next_note = transpose_current(s, context, direction * step_size);
    }
    s.current_note = next_note;
  }

  std::int32_t note = static_cast<std::int32_t>(s.current_note);
  if (s.tension > 0 && seeded::random_int(seed + 3, 99, 0) < s.tension) note += direction;
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  // All-or-nothing (deviation W4): the note-on is checked first, so a refusal leaves the previous
  // note sounding; the release comes next, and the note-on follows it.
  const Note on{static_cast<std::uint8_t>(note), s.last_velocity, 0};
  if (static_cast<std::size_t>(output.on.count) >= kPolyphony) {
    ++s.output_overflow;
    (void)output.on.append(on);  // Sets the batch's sticky overflow; nothing is stored.
    return;
  }
  if (s.has_active) {
    if (output.off.append(Note{s.active.pitch, 0, s.active.channel}) != PushResult::Added) {
      ++s.output_overflow;
      return;
    }
    s.has_active = false;
  }
  (void)output.on.append(on);
  s.has_active = true;
  s.active = on;
  s.active_due = context.now.ms + gate_ms(s);
}

// Parm.set_value semantics for a parameter with minmax: the value is clamped, not rejected.
bool clamp_int(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer) return false;
  const auto v = value.value.integer;
  out = v < low ? low : (v > high ? high : v);
  return true;
}

// Parameters without minmax reject out-of-range values (the source would index past a table).
bool int_in(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer && value.type != ParamType::Enumeration) return false;
  if (value.value.integer < low || value.value.integer > high) return false;
  out = value.value.integer;
  return true;
}

}  // namespace

void construct_wander_state(ModuleState& state) noexcept { new (&state.storage.wander) WanderModuleState{}; }

// Source WanderModule.process.
void process_wander(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.wander;
  output = {};
  const std::int64_t tick = unwrapped_tick(s, context.transport.tick);
  const bool running = context.transport.running;
  const bool was_running = s.was_running;
  const std::int64_t bin = floor_div(tick, rate_ticks(s));

  bool got_note_on = false;
  if (input.on.count > 0 || input.off.count > 0) got_note_on = update_held(s, input);
  if (got_note_on && running && was_running) s.last_grid_bin = bin;

  if (s.held.count == 0) {
    release_active(s, context.now.ms, true, output);
    s.was_running = running;
    return;
  }

  release_active(s, context.now.ms, false, output);

  if (running && !was_running) {
    s.last_grid_bin = -1;
    if (s.retrigger_mode == kRetriggerRetrigger) reset_walk(s);
  }

  if (running) {
    // A bin before the stored one restarts the grid (utils.grid_change), then one step per new bin.
    if (bin < s.last_grid_bin) s.last_grid_bin = -1;
    if (bin != s.last_grid_bin) {
      s.last_grid_bin = bin;
      generate_note(s, context, bin, output);
    }
  }
  s.was_running = running;
}

// Source WanderModule.stop. The sounding note is dropped without a release (deviation W5). Stop and
// Panic keep the center and velocity; Init and SceneChange restore a fresh instance.
void reset_wander_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::ParameterChange) return;
  auto& s = state.storage.wander;
  if (reason == ResetReason::Init || reason == ResetReason::SceneChange) {
    s.center_note = 60;
    s.last_velocity = 127;
  }
  s.held.clear();
  s.has_active = false;
  reset_walk(s);
  s.was_running = false;
}

bool set_wander_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&,
                      std::uint8_t& effects) noexcept {
  effects = static_cast<std::uint8_t>(EffectNone);
  auto& s = state.storage.wander;
  std::int32_t v = 0;
  switch (index) {
    case 0:  // RATE: no minmax in the source; out-of-range would index past the rate table.
      if (!int_in(value, 0, kRateIndexMax, v)) return false;
      s.rate_index = static_cast<std::uint8_t>(v);
      return true;
    case 1:
      if (!clamp_int(value, 0, 100, v)) return false;
      s.gravity = v;
      return true;
    case 2:
      if (!clamp_int(value, 0, 16, v)) return false;
      s.min_step = v;
      return true;
    case 3:
      if (!clamp_int(value, 0, 16, v)) return false;
      s.max_step = v;
      return true;
    case 4:
      if (!clamp_int(value, 0, 24, v)) return false;
      s.max_deviation = v;
      return true;
    case 5:
      if (!clamp_int(value, 0, 100, v)) return false;
      s.tension = v;
      return true;
    case 6:
      if (!clamp_int(value, 0, 10000, v)) return false;
      s.random_seed_user = v;
      return true;
    case 7:
      if (!clamp_int(value, 0, 128, v)) return false;
      s.loop_length = v;
      return true;
    case 8:  // RTRG: no minmax; out-of-range would index past the option names.
      if (!int_in(value, 0, 2, v)) return false;
      s.retrigger_mode = static_cast<std::uint8_t>(v);
      return true;
    case 9:  // RTHM: no minmax; out-of-range would index past the pattern table.
      if (!int_in(value, 0, kPatternMax, v)) return false;
      s.pattern = static_cast<std::uint8_t>(v);
      return true;
    case 10: {
      if (value.type != ParamType::Decimal || std::isnan(value.value.decimal)) return false;
      const float g = value.value.decimal;
      s.gate = g < -kGateLimit ? -kGateLimit : (g > kGateLimit ? kGateLimit : g);
      return true;
    }
    default:
      return false;
  }
}

void wander_attributes(ModuleState&, AttributeVisitor&) noexcept {}

const ModuleDescriptor& wander_module_descriptor() noexcept { return kWanderDescriptor; }

}  // namespace orbit
