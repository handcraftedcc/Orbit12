#include "arp_walk.hpp"
#include "../drop/drop_randomize.hpp"
#include "../../../domain/music.hpp"
#include "../../../domain/time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

// Deviations from circuitpython-source/src/apps/Orion/modules/arp_walk.py (each is also listed
// in the t09f report):
//  1. An emission is all-or-nothing, as in arp. The source appends the note-on to a 12-entry batch
//     and its release to a 12-entry schedule independently, so a full schedule leaves a stuck note.
//     Here a refused emission sends neither and is counted (schedule_overflow / output_overflow).
//     Output differs from the source only in passes where the source's schedule or batch overflows.
//  2. Due releases are popped before this pass generates notes (the source pops at the end). A
//     new release is due at now + gate >= now and is never popped in the pass that created it, so
//     the two orders differ only when a due release frees a slot that the source would have
//     refused (its stuck-note case).
//  3. The grid index and the randomize seed use the uint32 transport tick (source: unbounded).
//  4. The randomize seed uses the transport tick seen by the most recent process pass. The source
//     reads the tick at the button press, and set_param has no ProcessContext. The seeds agree when
//     the press follows a pass at the same tick.
//  5. A pass with more than kMaxExplicitTriggers grid triggers (a rate change or transport jump)
//     runs the explicit trigger loop for its first kMaxExplicitTriggers only. The remainder is
//     applied in closed form: step, pattern and walk counters are exact, and no note is emitted
//     (capacity is full after the explicit window). A chord walk correction that the source would
//     run inside that tail is not applied. Counted in tail_fast_forward.
//  6. A chord walk correction loop is cut after kMaxWalkCorrections iterations and counted in
//     walk_limit. The loop is bounded by the walk window (see the analysis in the report), so this
//     cap is not expected to be reached.
//  7. Stop, SceneChange, Panic and Init drop scheduled releases without sending them, as in the
//     source. The controller must call Output::all_notes_off on those transitions; otherwise these
//     notes stick.
//  8. Construction cannot read transport.running (no context). was_running starts false, where the
//     source takes transport.running at construction.
// Exact, not deviations:
//  - Single-note walk: music::transpose clamps to 0..127 as in source, so the source's single-note
//    correction loop never runs.
//  - Chord correction with a constant bounded walk (clamp mode) is skipped to the clamp boundary.
//    The loop's state depends only on the bounded value, so the result is identical.
//  - Transpose degrees are clamped to +-4096 before music::transpose. Larger magnitudes saturate.
//  - Hash-based random steps use seeded::random_int, which is exact for the source hash.
//  - set_param clamps where source Parm.set_value clamps (parameters with minmax). It rejects where
//    the source would index out of range (rate, sort, boundary, retrigger, pattern).

namespace orbit {
namespace {
constexpr std::uint8_t kSortDown = 1;
constexpr std::uint8_t kBoundaryClamp = 4;
constexpr std::uint8_t kRetriggerRtrg = 0;
constexpr std::uint8_t kRetriggerStable = 2;
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr float kGateMax = 100000.0f;
constexpr std::int64_t kTransposeDegreeLimit = 4096;
constexpr std::int32_t kStepValueLimit = 24;
constexpr std::int32_t kRandomStepMax = 3;
// Explicit trigger iterations per pass; the remainder is applied in closed form (deviation 5).
constexpr std::uint64_t kMaxExplicitTriggers = 256;
constexpr std::uint32_t kMaxWalkCorrections = 4096;
constexpr std::uint8_t kMaxSteps = 8;
constexpr auto kCapacity = static_cast<std::uint16_t>(kPolyphony);
constexpr std::uint8_t kCapacityByte = static_cast<std::uint8_t>(kPolyphony);

constexpr const char* kRateLabel = "RATE";
constexpr const char* kSortLabel = "SORT";
constexpr const char* kLengthLabel = "LEN";
constexpr const char* kSelectLabel = "SEL";
constexpr const char* kValueLabel = "VAL";
constexpr const char* kRandomLabel = "RAND";
constexpr const char* kInvertLabel = "INV";
constexpr const char* kMaxWalkLabel = "MAXWLK";
constexpr const char* kBoundaryLabel = "BND";
constexpr const char* kGateLabel = "GATE";
constexpr const char* kPatternLabel = "RYTM";
constexpr const char* kPatternShiftLabel = "RYSHFT";
constexpr const char* kRetriggerLabel = "RTRG";

constexpr const char* kSortOptions[] = {"UP", "DOWN", "ORD"};
constexpr const char* kBoundaryOptions[] = {"WRPU", "WRPB", "FLDU", "FLDB", "CLMP"};
constexpr const char* kRetriggerOptions[] = {"RTRG", "CONT", "STBL"};

const ParamSpec kArpWalkParams[] = {
    {"rate", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(12), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
    {"sort_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kSortLabel, kSortOptions, 3, ParamPersist},
    {"step_length", ParamType::Integer, ParamValue::integer(2), ParamValue::integer(1), ParamValue::integer(8), ParamValue::integer(1), kLengthLabel, nullptr, 0, ParamPersist},
    {"step_select", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(7), ParamValue::integer(1), kSelectLabel, nullptr, 0, ParamPersist},
    {"step_value", ParamType::Integer, ParamValue::integer(-1), ParamValue::integer(-24), ParamValue::integer(24), ParamValue::integer(1), kValueLabel, nullptr, 0, ParamPersist},
    {"randomize_steps", ParamType::Action, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), kRandomLabel, nullptr, 0, ParamUiOnly},
    {"invert", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), kInvertLabel, nullptr, 0, ParamPersist},
    {"max_walk", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(48), ParamValue::integer(1), kMaxWalkLabel, nullptr, 0, ParamPersist},
    {"boundary_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(4), ParamValue::integer(1), kBoundaryLabel, kBoundaryOptions, 5, ParamPersist},
    {"gate", ParamType::Decimal, ParamValue::decimal(100.0f), ParamValue::decimal(0.0f), ParamValue::decimal(kGateMax), ParamValue::decimal(5.0f), kGateLabel, nullptr, 0, ParamPersist},
    {"rhythm_pattern", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(24), ParamValue::integer(1), kPatternLabel, nullptr, 0, ParamPersist},
    {"pattern_shift", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kPatternShiftLabel, nullptr, 0, ParamPersist},
    {"retrigger_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kRetriggerLabel, kRetriggerOptions, 3, ParamPersist},
};

void descriptor_destroy(ModuleState&) noexcept {}
const ModuleDescriptor kArpWalkDescriptor{"arp_walk", 1, kArpWalkParams, 13, construct_arp_walk_state, descriptor_destroy, process_arp_walk, reset_arp_walk_state, set_arp_walk_param, arp_walk_attributes};

// Source ticks compare modularly; a deadline is reached when now - due is not negative.
bool wrap_less(std::uint32_t a, std::uint32_t b) noexcept { return static_cast<std::int32_t>(a - b) < 0; }

bool stop_like(ResetReason reason) noexcept {
  return reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic ||
         reason == ResetReason::Init;
}

bool int_in(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer && value.type != ParamType::Enumeration) return false;
  if (value.value.integer < low || value.value.integer > high) return false;
  out = value.value.integer;
  return true;
}

// Parm.set_value semantics for a parameter with minmax: the value is clamped, not rejected.
bool clamped_int(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer) return false;
  const auto v = value.value.integer;
  out = v < low ? low : (v > high ? high : v);
  return true;
}

// Python floor modulo for a positive modulus.
std::int64_t floor_mod(std::int64_t value, std::int64_t modulus) noexcept {
  const auto remainder = value % modulus;
  return remainder < 0 ? remainder + modulus : remainder;
}

// Python floor division for a positive divisor.
std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  return (value - floor_mod(value, divisor)) / divisor;
}

// get_pattern_step(bits, length, position + shift): MSB-first bit order, floor modulo.
bool pattern_on(const ArpWalkModuleState& s, std::uint64_t position) noexcept {
  const auto length = static_cast<std::int64_t>(music::kPatternLengths[s.pattern]);
  const auto bits = music::kPatternBits[s.pattern];
  const auto index = floor_mod(static_cast<std::int64_t>(position) + s.pattern_shift, length);
  return ((bits >> (length - index - 1)) & 1U) != 0;
}

// Source BOUNDARY modes (utils.bound_offset). Limit <= 0 leaves the offset unchanged.
std::int64_t bound_offset(std::int64_t offset, std::int32_t limit, std::uint8_t mode) noexcept {
  if (limit <= 0) return offset;
  const auto lim = static_cast<std::int64_t>(limit);
  if (mode == kBoundaryClamp) return offset < -lim ? -lim : (offset > lim ? lim : offset);
  if (mode == 0) return floor_mod(offset, lim + 1);
  if (mode == 1) return floor_mod(offset + lim, lim * 2 + 1) - lim;
  if (mode == 2) {
    const auto period = lim * 2;
    const auto folded = floor_mod(offset, period);
    return folded <= lim ? folded : period - folded;
  }
  const auto period = lim * 4;
  const auto folded = floor_mod(offset, period);
  if (folded <= lim) return folded;
  if (folded <= lim * 3) return lim * 2 - folded;
  return folded - period;
}

// Source ArpWalk.bounded_walk_position.
std::int64_t bounded_walk(const ArpWalkModuleState& s) noexcept {
  if (s.invert) return -bound_offset(-s.walk_position, s.max_walk, s.boundary_mode);
  return bound_offset(s.walk_position, s.max_walk, s.boundary_mode);
}

// Source ArpWalk.note_for_single: music.transpose clamps to 0..127, so no correction is needed.
std::uint8_t transpose_root(const ArpWalkModuleState& s, const ProcessContext& context, std::uint8_t root) noexcept {
  auto degrees = bounded_walk(s);
  if (degrees > kTransposeDegreeLimit) degrees = kTransposeDegreeLimit;
  if (degrees < -kTransposeDegreeLimit) degrees = -kTransposeDegreeLimit;
  return music::transpose(root, static_cast<std::int32_t>(degrees), 0, true, context.global.key, context.global.scale);
}

std::uint32_t gate_ms(const ArpWalkModuleState& s) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(s.gate));
}

std::uint32_t rate_ticks(const ArpWalkModuleState& s) noexcept {
  return static_cast<std::uint32_t>(music::kRateMidiTicks[s.rate_index]);
}

std::int32_t find_pitch(const NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch == pitch) return static_cast<std::int32_t>(i);
  }
  return -1;
}

bool has_pitch(const NoteBuffer& buffer, std::uint8_t pitch) noexcept { return find_pitch(buffer, pitch) >= 0; }

// Removes the oldest entry with this pitch (NoteArray.remove_value_first, order 0).
void remove_first_pitch(NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  const auto index = find_pitch(buffer, pitch);
  if (index < 0) return;
  for (auto i = static_cast<std::uint16_t>(index + 1); i < buffer.count; ++i) buffer.items[i - 1] = buffer.items[i];
  --buffer.count;
}

// NoteArray.sort_notes: stable insertion sort by pitch, ascending.
void sort_ascending(NoteBuffer& buffer) noexcept {
  for (std::uint16_t i = 1; i < buffer.count; ++i) {
    const Note key = buffer.items[i];
    std::uint16_t j = i;
    while (j > 0 && buffer.items[j - 1].pitch > key.pitch) {
      buffer.items[j] = buffer.items[j - 1];
      --j;
    }
    buffer.items[j] = key;
  }
}

void reverse_notes(NoteBuffer& buffer) noexcept {
  if (buffer.count < 2) return;
  std::uint16_t left = 0;
  auto right = static_cast<std::uint16_t>(buffer.count - 1);
  while (left < right) {
    const Note tmp = buffer.items[left];
    buffer.items[left] = buffer.items[right];
    buffer.items[right] = tmp;
    ++left;
    --right;
  }
}

// Source ArpWalk.update_note_register.
void update_notes(ArpWalkModuleState& s, const NoteBatch& input) noexcept {
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note& note = input.on.items[i];
    if (s.held.count < kCapacity) {
      (void)s.held.append(note);
    } else {
      ++s.held_overflow;  // Source drops silently.
    }
    if (!has_pitch(s.notes, note.pitch)) {
      if (s.notes.count < kCapacity) {
        (void)s.notes.append(note);
      } else {
        ++s.register_overflow;  // Source drops silently.
      }
    }
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const std::uint8_t pitch = input.off.items[i].pitch;
    if (has_pitch(s.held, pitch)) remove_first_pitch(s.held, pitch);
    if (has_pitch(s.notes, pitch) && !has_pitch(s.held, pitch)) remove_first_pitch(s.notes, pitch);
  }
  if (s.sort_mode == 0) {
    sort_ascending(s.notes);
  } else if (s.sort_mode == kSortDown) {
    sort_ascending(s.notes);
    reverse_notes(s.notes);
  }
}

// Due releases leave in schedule order (deviation 2: before this pass generates notes).
void pop_due_offs(ArpWalkModuleState& s, std::uint32_t now, NoteBatch& output) noexcept {
  std::uint8_t kept = 0;
  for (std::uint8_t i = 0; i < s.scheduled_count; ++i) {
    const ArpScheduled entry = s.scheduled[i];
    if (wrap_less(entry.due, now) && output.off.count < kCapacity) {
      (void)output.off.append(entry.note);
      continue;
    }
    s.scheduled[kept++] = entry;
  }
  s.scheduled_count = kept;
}

// Deviation 1: the note and its release are queued together or not at all.
void emit(ArpWalkModuleState& s, NoteBatch& output, const Note& note, std::uint32_t due) noexcept {
  if (s.scheduled_count >= kCapacityByte) {
    ++s.schedule_overflow;
    return;
  }
  if (output.on.count >= kCapacity) {
    ++s.output_overflow;
    return;
  }
  s.scheduled[s.scheduled_count++] = {Note{note.pitch, 0, note.channel}, due};
  (void)output.on.append(note);
}

// Source apply_step: the walk moves by the selected step value, signed when inverted.
void apply_step(ArpWalkModuleState& s) noexcept {
  const auto length = static_cast<std::uint64_t>(s.step_length);
  const auto value = static_cast<std::int64_t>(s.step_values[s.step_position % length]);
  s.walk_position += s.invert ? -value : value;
  ++s.step_position;
}

// Source ArpWalk.note_for_chord: index and octave from the bounded walk. A note outside 0..127
// moves the walk one step back toward range and retries (source while-loop).
bool choose_chord_note(ArpWalkModuleState& s, Note& chosen) noexcept {
  const auto count = static_cast<std::int64_t>(s.notes.count);
  const auto lim = static_cast<std::int64_t>(s.max_walk);
  for (std::uint32_t corrections = 0;; ++corrections) {
    if (corrections >= kMaxWalkCorrections) {
      ++s.walk_limit;
      return false;
    }
    const auto position = bounded_walk(s);
    const auto index = floor_mod(position, count);
    const auto octave = floor_div(position, count);
    const auto& held = s.notes.items[static_cast<std::size_t>(index)];
    const auto note = static_cast<std::int64_t>(held.pitch) + octave * 12;
    if (note > 127) {
      // Clamp mode: every walk >= max_walk gives the same bounded position, so the walk can be
      // moved to the boundary without changing the result.
      if (s.boundary_mode == kBoundaryClamp && lim > 0 && s.walk_position > lim) {
        s.walk_position = lim;
      } else {
        --s.walk_position;
      }
    } else if (note < 0) {
      if (s.boundary_mode == kBoundaryClamp && lim > 0 && s.walk_position < -lim) {
        s.walk_position = -lim;
      } else {
        ++s.walk_position;
      }
    } else {
      chosen = Note{static_cast<std::uint8_t>(note), held.velocity, held.channel};
      return true;
    }
  }
}

// Source ArpWalk.generate_notes for one grid trigger. The walk is updated even when the emission
// is refused, as in the source.
void generate_one(ArpWalkModuleState& s, const ProcessContext& context, NoteBatch& output) noexcept {
  if (s.notes.count == 0) return;  // Unreachable: arp_state requires a non-empty register.
  const auto now = context.now.ms;
  const auto timing_tick = static_cast<std::uint64_t>(context.transport.tick / rate_ticks(s));
  if (s.retrigger_mode == kRetriggerStable) s.active_step = timing_tick;
  const bool check = pattern_on(s, s.active_step);
  ++s.active_step;
  if (!check) return;

  apply_step(s);
  Note chosen{};
  if (s.notes.count == 1) {
    const auto& root = s.notes.items[0];
    const auto pitch = transpose_root(s, context, root.pitch);
    chosen = Note{pitch, root.velocity, root.channel};
  } else if (!choose_chord_note(s, chosen)) {
    return;  // Walk correction cap reached (counted); no emission.
  }
  emit(s, output, chosen, add_deadline(now, gate_ms(s)));
}

// Number of pattern hits over `count` consecutive steps starting at `start`.
std::uint64_t ones_in_window(const ArpWalkModuleState& s, std::uint64_t start, std::uint64_t count) noexcept {
  const auto length = static_cast<std::uint64_t>(music::kPatternLengths[s.pattern]);
  std::uint64_t full = 0;
  for (std::uint64_t x = 0; x < length; ++x) {
    if (pattern_on(s, start + x)) ++full;
  }
  std::uint64_t ones = (count / length) * full;
  const auto remainder = count % length;
  for (std::uint64_t x = 0; x < remainder; ++x) {
    if (pattern_on(s, start + x)) ++ones;
  }
  return ones;
}

// Sum of `count` consecutive step values from step position `start` (indexes wrap at the length).
std::int64_t step_sum(const ArpWalkModuleState& s, std::uint64_t start, std::uint64_t count) noexcept {
  const auto length = static_cast<std::uint64_t>(s.step_length);
  std::int64_t total = 0;
  for (std::uint64_t k = 0; k < length; ++k) total += s.step_values[k];
  std::int64_t sum = static_cast<std::int64_t>(count / length) * total;
  const auto remainder = count % length;
  for (std::uint64_t k = 0; k < remainder; ++k) sum += s.step_values[(start + k) % length];
  return sum;
}

// Closed form for the trigger tail (deviation 5). No emission is possible here: the explicit
// window has produced at least twelve hits or a constant hit, which fills the batch and schedule.
void fast_forward_tail(ArpWalkModuleState& s, const ProcessContext& context, std::uint64_t triggers) noexcept {
  std::uint64_t hits = 0;
  if (s.retrigger_mode == kRetriggerStable) {
    // Every trigger in this pass reads the same timing tick.
    const auto timing_tick = static_cast<std::uint64_t>(context.transport.tick / rate_ticks(s));
    s.active_step = timing_tick + 1;
    if (pattern_on(s, timing_tick)) hits = triggers;
  } else {
    hits = ones_in_window(s, s.active_step, triggers);
    s.active_step += triggers;
  }
  if (hits == 0) return;
  const auto sum = step_sum(s, s.step_position, hits);
  s.walk_position += s.invert ? -sum : sum;
  s.step_position += hits;
}

// Source ArpWalk.process grid loop: `triggers` generate_notes calls, bounded work.
void generate_triggers(ArpWalkModuleState& s, const ProcessContext& context, NoteBatch& output,
                       std::uint64_t triggers) noexcept {
  const auto explicit_count = triggers < kMaxExplicitTriggers ? triggers : kMaxExplicitTriggers;
  for (std::uint64_t i = 0; i < explicit_count; ++i) generate_one(s, context, output);
  const auto rest = triggers - explicit_count;
  if (rest > 0) {
    fast_forward_tail(s, context, rest);
    ++s.tail_fast_forward;
  }
}

// Source ArpWalk.randomize_steps. The seed uses the tick of the most recent process pass (deviation 4).
void randomize_steps(ArpWalkModuleState& s) noexcept {
  s.random_counter += 1;
  const auto seed = s.random_counter + static_cast<std::uint64_t>(s.last_tick);
  for (std::uint8_t i = 0; i < s.step_length; ++i) {
    s.step_values[i] = static_cast<std::int32_t>(seeded::random_int(seed + i, kRandomStepMax, static_cast<std::uint8_t>(1)));
  }
}

// Source ArpWalk.stop position reset (and the RTRG reset in process).
void reset_positions(ArpWalkModuleState& s) noexcept {
  s.walk_position = 0;
  s.step_position = 0;
  s.active_step = 0;
}

}  // namespace

void construct_arp_walk_state(ModuleState& state) noexcept { new (&state.storage.arp_walk) ArpWalkModuleState{}; }

// Source ArpWalk.process.
void process_arp_walk(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.arp_walk;
  output = {};
  const bool running = context.transport.running;
  s.last_tick = context.transport.tick;
  pop_due_offs(s, context.now.ms, output);

  std::uint16_t counter = 0;
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    if (has_pitch(s.notes, input.off.items[i].pitch)) ++counter;
  }
  const bool same_frame_retrigger = counter == s.notes.count;

  if (input.on.count > 0 || input.off.count > 0) update_notes(s, input);

  if ((s.arp_state == 1 && s.notes.count < 1) || same_frame_retrigger) {
    s.arp_state = 0;
    if (s.retrigger_mode == kRetriggerRtrg) reset_positions(s);
  }
  if (s.arp_state == 0 && s.notes.count > 0) s.arp_state = 1;

  if (running && !s.was_running) {
    s.last_grid_bin = -1;
    if (s.retrigger_mode == kRetriggerRtrg) reset_positions(s);
  }

  if (running) {
    const auto bin = static_cast<std::int64_t>(context.transport.tick / rate_ticks(s));
    // utils.grid_change: a bin before the stored one restarts the grid.
    const std::int64_t last = bin < s.last_grid_bin ? -1 : s.last_grid_bin;
    const bool changed = bin != last;
    const std::int64_t triggers = last < 0 ? 1 : bin - last;
    s.last_grid_bin = bin;
    if (changed && s.arp_state == 1) {
      generate_triggers(s, context, output, static_cast<std::uint64_t>(triggers));
    }
  }
  s.was_running = running;
}

// Source ArpWalk.stop. Pending releases are dropped; the controller releases sounding notes.
// step_values, the random counter and all parameters survive, as in the source.
void reset_arp_walk_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.arp_walk;
  s.held.clear();
  s.notes.clear();
  s.scheduled_count = 0;
  s.arp_state = 0;
  reset_positions(s);
  s.last_grid_bin = -1;
  s.was_running = false;
}

bool set_arp_walk_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&,
                        std::uint8_t& effects) noexcept {
  effects = static_cast<std::uint8_t>(EffectNone);
  auto& s = state.storage.arp_walk;
  std::int32_t v = 0;
  switch (index) {
    case 0:
      if (!int_in(value, 0, 12, v)) return false;
      s.rate_index = static_cast<std::uint8_t>(v);
      return true;
    case 1:
      if (!int_in(value, 0, 2, v)) return false;
      s.sort_mode = static_cast<std::uint8_t>(v);
      return true;
    case 2:  // step_length: source clamps to 1..8, then the select clamps down.
      if (!clamped_int(value, 1, kMaxSteps, v)) return false;
      s.step_length = static_cast<std::uint8_t>(v);
      if (s.step_select >= s.step_length) s.step_select = static_cast<std::uint8_t>(s.step_length - 1);
      effects = static_cast<std::uint8_t>(EffectRepaint);
      return true;
    case 3:  // step_select: source clamps to the current 0..step_length-1.
      if (!clamped_int(value, 0, static_cast<std::int32_t>(s.step_length) - 1, v)) return false;
      s.step_select = static_cast<std::uint8_t>(v);
      effects = static_cast<std::uint8_t>(EffectRepaint);
      return true;
    case 4:  // step_value: stored at the selected step.
      if (!clamped_int(value, -kStepValueLimit, kStepValueLimit, v)) return false;
      s.step_values[s.step_select] = v;
      return true;
    case 5:  // randomize_steps: button action.
      if (value.type != ParamType::Action) return false;
      randomize_steps(s);
      effects = static_cast<std::uint8_t>(EffectRepaint);
      return true;
    case 6:
      if (value.type != ParamType::Boolean) return false;
      s.invert = value.value.boolean;
      return true;
    case 7:
      if (!clamped_int(value, 0, 48, v)) return false;
      s.max_walk = v;
      return true;
    case 8:
      if (!int_in(value, 0, 4, v)) return false;
      s.boundary_mode = static_cast<std::uint8_t>(v);
      return true;
    case 9:
      if (value.type != ParamType::Decimal || std::isnan(value.value.decimal)) return false;
      s.gate = value.value.decimal < 0.0f ? 0.0f : (value.value.decimal > kGateMax ? kGateMax : value.value.decimal);
      return true;
    case 10:
      if (!int_in(value, 0, 24, v)) return false;
      s.pattern = static_cast<std::uint8_t>(v);
      return true;
    case 11:
      if (value.type != ParamType::Integer) return false;
      s.pattern_shift = value.value.integer;
      return true;
    case 12:
      if (!int_in(value, 0, 2, v)) return false;
      s.retrigger_mode = static_cast<std::uint8_t>(v);
      return true;
    default:
      return false;
  }
}

// Saved attribute `step_values` (source save_attrs). Fixed element count of eight.
void arp_walk_attributes(ModuleState& state, AttributeVisitor& v) noexcept {
  if (!v.visit) return;
  auto& s = state.storage.arp_walk;
  AttributeDescriptor attr{"step_values", AttributeType::Integer, static_cast<std::uint16_t>(kMaxSteps), s.step_values};
  (void)v.visit(v.ctx, attr);
}

const ModuleDescriptor& arp_walk_module_descriptor() noexcept { return kArpWalkDescriptor; }
}  // namespace orbit
