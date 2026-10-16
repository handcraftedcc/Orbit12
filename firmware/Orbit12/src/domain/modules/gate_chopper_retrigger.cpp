#include "gate_chopper_retrigger.hpp"
#include "drop_randomize.hpp"
#include "../music.hpp"
#include "../time.hpp"
#include <cfloat>
#include <cstdint>
#include <limits>
#include <new>

// Deviations from the Python source (each is also listed in the migration notes):
//  1. Gate: a note-on whose off cannot be scheduled (12 scheduled offs) is refused and
//     counted. The source passes the on and silently drops the off, leaving a stuck note.
//  2. Gate: scheduled delays are clamped to +-(2**28 - 1) ms, half of the source's 2**29
//     tick period. Beyond that the source's own ordering is undefined.
//  3. Chopper: an on that cannot be emitted is not marked active. The source appends it to
//     active and silently drops the on. With deviation 7, a batch cannot exceed 12 ons, so
//     this is a defensive guard only.
//  4. Chopper: a release that cannot be queued keeps the note held and active. Defensive:
//     with 12-entry input batches the offs of one batch never exceed 12.
//  5. Retrigger: the retrigger is skipped when its off batch would exceed 12 entries.
//     The source silently drops the last held off, which leaves a stuck note.
//  6. Chopper blocked-step count is computed with float chop (ParamValue is float32).
//     The source uses a Python double; values near rounding boundaries can differ.
//  7. Chopper: when an evicted note's on is already in the same batch (grid open), the on
//     is cancelled and no off is sent. The source sends the off before the on, so the note
//     would sound forever. Held-set eviction and grid timing are otherwise unchanged.
// Source behavior kept as is: retrigger drops a new note when 12 notes are already held
// (counted in held_overflow), and its retrigger offs include the newly pressed note.

namespace orbit {
namespace {
constexpr std::uint8_t kNone = 0;
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr float kFloatMax = (std::numeric_limits<float>::max)();
// Half of the source's 2**29 tick period: deadlines more than this far ahead are not ordered.
constexpr std::int64_t kMaxDelayMs = (std::int64_t{1} << 28) - 1;
constexpr auto kCapacity = static_cast<std::uint16_t>(kPolyphony);

constexpr const char* kGateLabel = "GATE";
constexpr const char* kGateRandomLabel = "GTRND";
constexpr const char* kPatternLengthLabel = "PTLEN";
constexpr const char* kRateLabel = "RATE";
constexpr const char* kChopLabel = "CHOP";
constexpr const char* kSeedLabel = "SEED";
constexpr const char* kRetriggerLabel = "RTRG";
constexpr const char* kModeLabel = "MODE";
constexpr const char* kThresholdLabel = "THRSH";
constexpr const char* kChopperRetriggerOptions[] = {"STBL", "CONT", "RTRG"};
constexpr const char* kRetriggerModeOptions[] = {"PRESS", "REL", "BOTH"};

const ParamSpec kGateParams[] = {
    {"gate", ParamType::Integer, ParamValue::integer(100), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(5), kGateLabel, nullptr, 0, ParamPersist},
    {"gate_random", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(5), kGateRandomLabel, nullptr, 0, ParamPersist},
    {"pattern_length", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(128), ParamValue::integer(1), kPatternLengthLabel, nullptr, 0, ParamPersist}};

const ParamSpec kChopperParams[] = {
    {"rate", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(12), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
    {"chop", ParamType::Decimal, ParamValue::decimal(0.5f), ParamValue::decimal(-kFloatMax), ParamValue::decimal(kFloatMax), ParamValue::decimal(0.05f), kChopLabel, nullptr, 0, ParamPersist},
    {"pattern_length", ParamType::Integer, ParamValue::integer(16), ParamValue::integer(1), ParamValue::integer(128), ParamValue::integer(1), kPatternLengthLabel, nullptr, 0, ParamPersist},
    {"rand_seed", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(10000), ParamValue::integer(1), kSeedLabel, nullptr, 0, ParamPersist},
    {"retrigger_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kRetriggerLabel, kChopperRetriggerOptions, 3, ParamPersist}};

const ParamSpec kRetriggerParams[] = {
    {"mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kModeLabel, kRetriggerModeOptions, 3, ParamPersist},
    {"threshold", ParamType::Integer, ParamValue::integer(50), ParamValue::integer(0), ParamValue::integer(1000), ParamValue::integer(1), kThresholdLabel, nullptr, 0, ParamPersist}};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kGateDescriptor{"gate", 1, kGateParams, 3, construct_gate_state, descriptor_destroy, process_gate, reset_gate_state, set_gate_param, attr_none};
const ModuleDescriptor kChopperDescriptor{"chopper", 1, kChopperParams, 5, construct_chopper_state, descriptor_destroy, process_chopper, reset_chopper_state, set_chopper_param, attr_none};
const ModuleDescriptor kRetriggerDescriptor{"retrigger", 1, kRetriggerParams, 2, construct_retrigger_state, descriptor_destroy, process_retrigger, reset_retrigger_state, set_retrigger_param, attr_none};

// Source tick comparison: a signed modular difference, valid for deadlines under half the period.
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

bool decimal_in(const ParamValue& value, float& out) noexcept {
  if (value.type != ParamType::Decimal) return false;
  if (!(value.value.decimal >= -kFloatMax && value.value.decimal <= kFloatMax)) return false;
  out = value.value.decimal;
  return true;
}

std::int64_t clamp_delay(std::int64_t value) noexcept {
  if (value > kMaxDelayMs) return kMaxDelayMs;
  if (value < -kMaxDelayMs) return -kMaxDelayMs;
  return value;
}

std::int32_t find_first(const NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch == pitch) return static_cast<std::int32_t>(i);
  }
  return -1;
}

bool contains(const NoteBuffer& buffer, std::uint8_t pitch) noexcept { return find_first(buffer, pitch) >= 0; }

// Removes the oldest entry with this pitch, like NoteArray.remove_value_first.
bool remove_first(NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  const auto index = find_first(buffer, pitch);
  if (index < 0) return false;
  const auto at = static_cast<std::uint16_t>(index);
  for (std::uint16_t i = static_cast<std::uint16_t>(at + 1); i < buffer.count; ++i) buffer.items[i - 1] = buffer.items[i];
  --buffer.count;
  return true;
}

bool is_full(const NoteBuffer& buffer) noexcept { return buffer.count >= kCapacity; }

}  // namespace

void construct_gate_state(ModuleState& state) noexcept { new (&state.storage.gate) GateModuleState{}; }
void construct_chopper_state(ModuleState& state) noexcept { new (&state.storage.chopper) ChopperModuleState{}; }
void construct_retrigger_state(ModuleState& state) noexcept { new (&state.storage.retrigger) RetriggerModuleState{}; }

// Source Gate.process. Incoming offs are not forwarded; each on schedules its own off.
// Due offs are emitted first, in schedule order, before this batch's ons are scheduled.
void process_gate(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.gate;
  output = {};
  const auto now = context.now.ms;

  // pop_due: remove entries strictly earlier than now and keep the order of the rest.
  std::uint8_t kept = 0;
  for (std::uint8_t i = 0; i < s.scheduled_count; ++i) {
    const GateScheduledOff entry = s.scheduled[i];
    if (wrap_less(entry.due_ms, now) && output.off.append({entry.note.pitch, 0, entry.note.channel}) == PushResult::Added) {
      continue;
    }
    // Not emitted: the capacity of the off batch equals the schedule capacity, so an entry
    // is only kept here when it is not yet due. It is never dropped.
    s.scheduled[kept++] = entry;
  }
  s.scheduled_count = kept;

  auto step = static_cast<std::uint64_t>(context.transport.tick / 6U);
  if (s.pattern_length != 0) step %= static_cast<std::uint64_t>(s.pattern_length);
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note& note = input.on.items[i];
    const auto random_gate = seeded::random_int(step + i, s.gate_random, 0);
    const auto delay = clamp_delay(static_cast<std::int64_t>(s.gate) + random_gate);
    // Deviation 1: refuse the on when its off cannot be scheduled.
    if (s.scheduled_count >= kCapacity || is_full(output.on)) {
      ++s.schedule_overflow;
      continue;
    }
    (void)output.on.append(note);
    s.scheduled[s.scheduled_count++] = {note, add_deadline(now, static_cast<std::uint32_t>(delay))};
  }
}

// Source Chopper.process.
void process_chopper(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.chopper;
  output = {};
  const bool had_notes = s.held.count > 0;

  // Released notes leave held. A sounding note is released through the output first.
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const std::uint8_t pitch = input.off.items[i].pitch;
    if (contains(s.active, pitch)) {
      // Deviation 4: keep the note held and active if its release cannot be queued.
      if (is_full(output.off)) {
        ++s.output_overflow;
        continue;
      }
      (void)remove_first(s.active, pitch);
      (void)output.off.append({pitch, 0, input.off.items[i].channel});
    }
    (void)remove_first(s.held, pitch);
  }

  if (input.on.count > 0 && !had_notes && s.retrigger_mode == 2) {
    s.step_index = 0;
    s.last_grid_bin = -1;
  }

  if (context.transport.running) {
    const auto rate = static_cast<std::uint32_t>(music::kRateMidiTicks[s.rate_index]);
    const auto bin = static_cast<std::int64_t>(context.transport.tick / rate);
    // grid_change: a bin before the last one restarts the grid. The stored value changes
    // only when the branch below runs, as in the source.
    const std::int64_t last = bin < s.last_grid_bin ? -1 : s.last_grid_bin;
    const bool changed = bin != last;
    if (changed && (s.held.count > 0 || input.on.count > 0)) {
      s.last_grid_bin = bin;
      const std::uint64_t raw_step = s.retrigger_mode == 0 ? static_cast<std::uint64_t>(bin) : s.step_index++;
      const auto length = static_cast<std::uint64_t>(s.pattern_length);
      const auto step = raw_step % length;
      // int(chop * length + 0.5). Clamped to a range that gives the same comparison result.
      const double scaled = static_cast<double>(s.chop) * static_cast<double>(s.pattern_length) + 0.5;
      std::int64_t blocked_steps = 0;
      if (scaled <= -1.0) {
        blocked_steps = -1;
      } else if (scaled >= 129.0) {
        blocked_steps = 129;
      } else {
        blocked_steps = static_cast<std::int64_t>(scaled);
      }
      // Rank this step inside the deterministic shuffled pattern.
      const auto step_value = seeded::random_int(static_cast<std::uint64_t>(s.seed) + step, 65535, 0);
      std::int64_t rank = 0;
      for (std::uint64_t p = 0; p < length; ++p) {
        const auto value = seeded::random_int(static_cast<std::uint64_t>(s.seed) + p, 65535, 0);
        if (value < step_value || (value == step_value && p < step)) ++rank;
      }
      s.blocked = rank < blocked_steps;
      if (s.blocked) {
        // Closing the gate releases every sounding note. Any that cannot be queued stay active.
        NoteBuffer remaining{};
        for (std::uint16_t i = 0; i < s.active.count; ++i) {
          const Note note = s.active.items[i];
          if (output.off.append({note.pitch, 0, note.channel}) == PushResult::Added) continue;
          ++s.output_overflow;
          (void)remaining.append(note);
        }
        s.active = remaining;
      } else {
        for (std::uint16_t i = 0; i < s.held.count; ++i) {
          const Note note = s.held.items[i];
          if (contains(s.active, note.pitch)) continue;
          // Deviation 3: an on that cannot be emitted is not marked active.
          if (is_full(output.on) || is_full(s.active)) {
            ++s.output_overflow;
            continue;
          }
          (void)output.on.append({note.pitch, note.velocity, note.channel});
          (void)s.active.append(note);
        }
      }
    } else if (s.retrigger_mode != 0 && s.held.count == 0 && input.on.count == 0) {
      s.last_grid_bin = bin;
    }
  }

  // Incoming notes. A full held set evicts its oldest note, releasing it if sounding.
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note note = input.on.items[i];
    if (s.held.count >= kCapacity && !contains(s.held, note.pitch)) {
      const Note old = s.held.items[0];
      if (contains(s.active, old.pitch)) {
        // Deviation 7: an on for this note is already in this batch (grid open). Flush sends
        // offs before ons, so an off here would leave the note sounding forever. Cancel the
        // pending on and send no off. If an earlier retrigger off is in the batch, it stays.
        if (!remove_first(output.on, old.pitch)) {
          if (is_full(output.off)) {
            ++s.output_overflow;
            continue;
          }
          (void)output.off.append({old.pitch, 0, old.channel});
        }
        (void)remove_first(s.active, old.pitch);
      }
      (void)remove_first(s.held, old.pitch);
    }
    const auto index = find_first(s.held, note.pitch);
    if (index >= 0) {
      s.held.items[static_cast<std::uint16_t>(index)].velocity = note.velocity;
    } else {
      (void)s.held.append(note);
    }
    if (s.blocked) continue;
    if (contains(s.active, note.pitch)) {
      // Retrigger of a sounding note: off then on, both or neither.
      if (is_full(output.off) || is_full(output.on)) {
        ++s.output_overflow;
        continue;
      }
      (void)remove_first(s.active, note.pitch);
      (void)output.off.append({note.pitch, 0, note.channel});
    } else if (is_full(output.on) || is_full(s.active)) {
      ++s.output_overflow;
      continue;
    }
    (void)s.active.append(note);
    (void)output.on.append({note.pitch, note.velocity, note.channel});
  }
}

// Source Retrigger.process.
void process_retrigger(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.retrigger;
  output = {};
  const bool had_notes = s.held.count > 0;

  // Every held note is released by this batch: pass the batch through and clear held.
  // This path does not record a threshold event, as in the source.
  if (input.off.count > 0 && s.held.count > 0 && s.held.count == input.off.count) {
    bool all_off = true;
    for (std::uint16_t i = 0; i < s.held.count && all_off; ++i) all_off = contains(input.off, s.held.items[i].pitch);
    if (all_off) {
      for (std::uint16_t i = 0; i < input.off.count; ++i) (void)output.off.append(input.off.items[i]);
      for (std::uint16_t i = 0; i < input.on.count; ++i) (void)output.on.append(input.on.items[i]);
      s.held.clear();
      return;
    }
  }

  const bool press_event = (s.mode == 0 || s.mode == 2) && input.on.count > 0;
  const bool release_event = (s.mode == 1 || s.mode == 2) && input.off.count > 0;

  // update_held_notes: offs remove the oldest entry; a repeated on moves to the newest position.
  for (std::uint16_t i = 0; i < input.off.count; ++i) (void)remove_first(s.held, input.off.items[i].pitch);
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note& note = input.on.items[i];
    if (contains(s.held, note.pitch)) (void)remove_first(s.held, note.pitch);
    if (s.held.append(note) != PushResult::Added) ++s.held_overflow;
  }

  bool retrigger = false;
  if (press_event || release_event) {
    // note_event: the threshold is measured from the previous event, which is always updated.
    bool ready = true;
    if (s.threshold > 0 && s.has_last_event) {
      const auto next = s.last_event_ms + static_cast<std::uint32_t>(s.threshold);
      ready = !wrap_less(context.now.ms, next);
    }
    s.has_last_event = true;
    s.last_event_ms = context.now.ms;
    retrigger = (press_event ? had_notes : true) && s.held.count > 0 && ready;
  }

  for (std::uint16_t i = 0; i < input.off.count; ++i) (void)output.off.append(input.off.items[i]);
  if (retrigger) {
    // Deviation 5: the held offs must fit beside the batch's offs; otherwise skip the retrigger.
    if (static_cast<std::uint32_t>(output.off.count) + s.held.count <= kCapacity) {
      for (std::uint16_t i = 0; i < s.held.count; ++i) {
        (void)output.off.append({s.held.items[i].pitch, 0, s.held.items[i].channel});
      }
      for (std::uint16_t i = 0; i < s.held.count; ++i) (void)output.on.append(s.held.items[i]);
      return;
    }
    ++s.output_overflow;
  }
  for (std::uint16_t i = 0; i < input.on.count; ++i) (void)output.on.append(input.on.items[i]);
}

void reset_gate_state(ModuleState& state, ResetReason reason) noexcept {
  if (stop_like(reason)) state.storage.gate.scheduled_count = 0;
}

void reset_chopper_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.chopper;
  s.held.clear();
  s.active.clear();
  s.blocked = false;
  s.step_index = 0;
  s.last_grid_bin = -1;
}

void reset_retrigger_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.retrigger;
  s.held.clear();
  s.has_last_event = false;
  s.last_event_ms = 0;
}

bool set_gate_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.gate;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.gate = v; return true;
    case 1: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.gate_random = v; return true;
    case 2: if (!int_in(value, 0, 128, v)) return false; s.pattern_length = v; return true;
    default: return false;
  }
}

bool set_chopper_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.chopper;
  std::int32_t v = 0;
  float f = 0.0f;
  switch (index) {
    case 0: if (!int_in(value, 0, 12, v)) return false; s.rate_index = static_cast<std::uint8_t>(v); return true;
    case 1: if (!decimal_in(value, f)) return false; s.chop = f; return true;
    case 2: if (!int_in(value, 1, 128, v)) return false; s.pattern_length = v; return true;
    case 3: if (!int_in(value, 0, 10000, v)) return false; s.seed = v; return true;
    case 4: if (!int_in(value, 0, 2, v)) return false; s.retrigger_mode = static_cast<std::uint8_t>(v); return true;
    default: return false;
  }
}

bool set_retrigger_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.retrigger;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, 0, 2, v)) return false; s.mode = static_cast<std::uint8_t>(v); return true;
    case 1: if (!int_in(value, 0, 1000, v)) return false; s.threshold = v; return true;
    default: return false;
  }
}

const ModuleDescriptor& gate_module_descriptor() noexcept { return kGateDescriptor; }
const ModuleDescriptor& chopper_module_descriptor() noexcept { return kChopperDescriptor; }
const ModuleDescriptor& retrigger_module_descriptor() noexcept { return kRetriggerDescriptor; }
}  // namespace orbit
