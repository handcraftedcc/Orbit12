#include "euclid.hpp"
#include "../music.hpp"
#include <cstdint>
#include <limits>
#include <new>

// Deviations from the Python source (each is also listed in the migration notes):
//  1. A lane with length 0 is silent. Python divides by zero (an exception that stops the
//     chain). Length 0 is unreachable through set_euclid_param; it can only come from an
//     unvalidated attribute restore, so the guard makes the processor total.
//  2. Parameter values outside their ranges are rejected (set_euclid_param returns false).
//     Python's scene path clamps ints through set_value and accepts any enum value; an
//     out-of-range enum behaves as ORD/CONT in the source. Rejection is the documented
//     invalid-persistent-value policy.
//  3. GATE is float32 (ParamValue::decimal). Python float64 gives int(gate) on the same
//     values; the two differ only when a fractional persisted value sits within float32
//     rounding of an integer boundary. Integer GATE values are exact.
//  4. Held notes that cannot be recorded (13th distinct held pitch) are dropped as in the
//     source and counted in held_overflow. Their off would have matched nothing, so no
//     release is lost.
// Source behavior kept as is: output lists never exceed six entries (one on and at most
// one off per lane per pass), so no release is ever refused by the batch; step counters
// are unbounded in Python and 64-bit here (unreachable overflow).

namespace orbit {
namespace {
constexpr std::uint8_t kNoPitch = 255;
constexpr std::uint8_t kSortUp = 0;
constexpr std::uint8_t kSortDown = 1;
constexpr std::uint8_t kRetriggerStable = 0;
constexpr std::uint8_t kRetriggerRestart = 2;
constexpr std::int32_t kLaneMax = static_cast<std::int32_t>(kEuclidLanes) - 1;
constexpr std::int32_t kRateMax = 12;
constexpr std::int32_t kPulsesMax = 128;
constexpr std::int32_t kLengthMin = 1;
constexpr std::int32_t kLengthMax = 128;
constexpr std::int32_t kRotationMax = 127;
constexpr float kGateMax = 100000.0f;

constexpr const char* kLaneLabel = "LANE";
constexpr const char* kRateLabel = "RATE";
constexpr const char* kPulsesLabel = "PULSES";
constexpr const char* kLengthLabel = "LENGTH";
constexpr const char* kRotationLabel = "ROT";
constexpr const char* kSortLabel = "SORT";
constexpr const char* kGateLabel = "GATE";
constexpr const char* kRetriggerLabel = "RTRG";
constexpr const char* kSortOptions[] = {"UP", "DOWN", "ORD"};
constexpr const char* kRetriggerOptions[] = {"STBL", "CONT", "RTRG"};

const ParamSpec kEuclidParams[] = {
    {"lane", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(kLaneMax), ParamValue::integer(1), kLaneLabel, nullptr, 0, ParamPersist},
    {"rate", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(kRateMax), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
    {"pulses", ParamType::Integer, ParamValue::integer(4), ParamValue::integer(0), ParamValue::integer(kPulsesMax), ParamValue::integer(1), kPulsesLabel, nullptr, 0, ParamPersist},
    {"length", ParamType::Integer, ParamValue::integer(16), ParamValue::integer(kLengthMin), ParamValue::integer(kLengthMax), ParamValue::integer(1), kLengthLabel, nullptr, 0, ParamPersist},
    {"rotation", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(-kRotationMax), ParamValue::integer(kRotationMax), ParamValue::integer(1), kRotationLabel, nullptr, 0, ParamPersist},
    {"sort", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kSortLabel, kSortOptions, 3, ParamPersist},
    {"gate", ParamType::Decimal, ParamValue::decimal(100.0f), ParamValue::decimal(0.0f), ParamValue::decimal(kGateMax), ParamValue::decimal(5.0f), kGateLabel, nullptr, 0, ParamPersist},
    {"retrigger_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kRetriggerLabel, kRetriggerOptions, 3, ParamPersist}};

void descriptor_destroy(ModuleState&) noexcept {}
const ModuleDescriptor kEuclidDescriptor{"euclid", 1, kEuclidParams, 8, construct_euclid_state, descriptor_destroy,
                                         process_euclid, reset_euclid_state, set_euclid_param, euclid_attributes};

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
  if (!(value.value.decimal >= 0.0f && value.value.decimal <= kGateMax)) return false;
  out = value.value.decimal;
  return true;
}

// Source: int(gate). Gate values reaching this point passed decimal_in; anything else is no delay.
std::uint32_t gate_ms(float gate) noexcept {
  if (!(gate >= 0.0f && gate <= kGateMax)) return 0;
  return static_cast<std::uint32_t>(gate);
}

// Source: (step + rotation) % length with Python's floored modulo.
std::uint32_t lane_position(std::uint64_t step, std::int32_t rotation, std::uint32_t length) noexcept {
  const auto len = static_cast<std::int64_t>(length);
  std::int64_t rot = static_cast<std::int64_t>(rotation) % len;
  if (rot < 0) rot += len;
  return static_cast<std::uint32_t>((step % length + static_cast<std::uint64_t>(rot)) % length);
}

bool contains(const NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch == pitch) return true;
  }
  return false;
}

// Removes the oldest entry with this pitch, like NoteArray.remove_value_first.
void remove_first(NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch != pitch) continue;
    for (std::uint16_t j = static_cast<std::uint16_t>(i + 1); j < buffer.count; ++j) buffer.items[j - 1] = buffer.items[j];
    --buffer.count;
    return;
  }
}

// Source NoteArray.sort_notes (ascending insertion sort) and reverse_notes for SORT=DOWN.
void build_note_order(EuclidModuleState& s) noexcept {
  s.note_order.clear();
  for (std::uint16_t i = 0; i < s.held.count; ++i) (void)s.note_order.append(s.held.items[i]);
  if (s.sort_mode == kSortUp || s.sort_mode == kSortDown) {
    for (std::uint16_t i = 1; i < s.note_order.count; ++i) {
      const Note key = s.note_order.items[i];
      std::uint16_t j = i;
      while (j > 0 && s.note_order.items[j - 1].pitch > key.pitch) {
        s.note_order.items[j] = s.note_order.items[j - 1];
        --j;
      }
      s.note_order.items[j] = key;
    }
  }
  if (s.sort_mode == kSortDown && s.note_order.count > 1) {
    for (std::uint16_t left = 0, right = static_cast<std::uint16_t>(s.note_order.count - 1); left < right; ++left, --right) {
      const Note tmp = s.note_order.items[left];
      s.note_order.items[left] = s.note_order.items[right];
      s.note_order.items[right] = tmp;
    }
  }
}

// Source Euclid.update_held_notes. Offs are matched by pitch across all lanes, so one off can
// release several lanes (one off per matching lane, as in the source).
void update_held(EuclidModuleState& s, const NoteBatch& input, NoteBatch& output) noexcept {
  const bool had_notes = s.held.count > 0;
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const std::uint8_t pitch = input.off.items[i].pitch;
    remove_first(s.held, pitch);
    for (std::size_t lane = 0; lane < kEuclidLanes; ++lane) {
      if (s.active_pitch[lane] != pitch) continue;
      (void)output.off.append({pitch, 0, 0});
      s.active_pitch[lane] = kNoPitch;
    }
  }
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note& note = input.on.items[i];
    if (contains(s.held, note.pitch)) remove_first(s.held, note.pitch);
    if (s.held.append({note.pitch, note.velocity, 0}) != PushResult::Added) ++s.held_overflow;
  }
  if (input.on.count > 0 && !had_notes && s.retrigger_mode == kRetriggerRestart) {
    s.step_index = 0;
    s.last_grid_bin = -1;
  }
}

void release_lane(EuclidModuleState& s, std::size_t lane, NoteBatch& output) noexcept {
  (void)output.off.append({s.active_pitch[lane], 0, 0});
  s.active_pitch[lane] = kNoPitch;
}

// Source Euclid.process_lane.
void process_lane(EuclidModuleState& s, std::size_t lane, std::uint64_t step, std::uint32_t now, NoteBatch& output) noexcept {
  const std::uint32_t length = s.lengths[lane];
  const std::uint32_t pulses = s.pulses[lane] < length ? s.pulses[lane] : length;
  // Deviation 1: length 0 is silent (Python raises ZeroDivisionError).
  const bool hit = length != 0 && euclid_step(lane_position(step, s.rotations[lane], length), pulses, length);
  const std::uint8_t active = s.active_pitch[lane];

  if (lane >= s.note_order.count) {
    if (active != kNoPitch) release_lane(s, lane, output);
    return;
  }
  if (!hit) return;

  const Note note = s.note_order.items[lane];
  if (active != kNoPitch) (void)output.off.append({active, 0, 0});
  s.active_pitch[lane] = note.pitch;
  s.active_due[lane] = now + gate_ms(s.gate);
  (void)output.on.append({note.pitch, note.velocity, 0});
}

void stop_active(EuclidModuleState& s, NoteBatch& output) noexcept {
  for (std::size_t lane = 0; lane < kEuclidLanes; ++lane) {
    if (s.active_pitch[lane] != kNoPitch) release_lane(s, lane, output);
  }
}
}  // namespace

bool euclid_step(std::uint32_t step, std::uint32_t pulses, std::uint32_t steps) noexcept {
  if (steps == 0 || pulses == 0) return false;
  if (pulses >= steps) return true;
  return ((step + 1U) * pulses) / steps != (step * pulses) / steps;
}

void construct_euclid_state(ModuleState& state) noexcept { new (&state.storage.euclid) EuclidModuleState{}; }

// Source Euclid.process. Order: gate expiry, held-note update, transport-start reset, then
// either the grid pass (running with held notes) or release of every active lane.
void process_euclid(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.euclid;
  output = {};
  const bool running = context.transport.running;
  const std::uint32_t now = context.now.ms;

  for (std::size_t lane = 0; lane < kEuclidLanes; ++lane) {
    if (s.active_pitch[lane] != kNoPitch && wrap_less(s.active_due[lane], now)) release_lane(s, lane, output);
  }

  update_held(s, input, output);

  if (running && !s.was_running) {
    s.last_grid_bin = -1;
    if (s.retrigger_mode == kRetriggerRestart) s.step_index = 0;
  }

  if (running && s.held.count > 0) {
    build_note_order(s);
    const auto bin = static_cast<std::int64_t>(context.transport.tick / music::kRateMidiTicks[s.rate_index]);
    // grid_change: the grid moved when the bin differs from the last one (a backwards bin
    // counts as a change). The stored bin is always the current one.
    const bool changed = bin != s.last_grid_bin;
    s.last_grid_bin = bin;
    if (changed) {
      std::uint64_t step = 0;
      if (s.retrigger_mode == kRetriggerStable) {
        step = static_cast<std::uint64_t>(bin);
      } else {
        step = s.step_index;
        ++s.step_index;
      }
      for (std::size_t lane = 0; lane < kEuclidLanes; ++lane) process_lane(s, lane, step, now, output);
    }
  } else {
    stop_active(s, output);
  }

  s.was_running = running;
}

// Source Euclid.stop clears held notes, the note order and lane voices, and the grid. Sounding
// notes are released by the chain's all-notes-off on stop, as for the other processors.
void reset_euclid_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.euclid;
  s.held.clear();
  s.note_order.clear();
  for (std::size_t lane = 0; lane < kEuclidLanes; ++lane) {
    s.active_pitch[lane] = kNoPitch;
    s.active_due[lane] = 0;
  }
  s.step_index = 0;
  s.last_grid_bin = -1;
  s.was_running = false;
}

bool set_euclid_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = EffectNone;
  auto& s = state.storage.euclid;
  std::int32_t v = 0;
  float f = 0.0f;
  switch (index) {
    case 0:
      if (!int_in(value, 0, kLaneMax, v)) return false;
      s.lane = static_cast<std::uint8_t>(v);
      effects = EffectRepaint;
      return true;
    case 1:
      if (!int_in(value, 0, kRateMax, v)) return false;
      s.rate_index = static_cast<std::uint8_t>(v);
      return true;
    case 2:
      if (!int_in(value, 0, kPulsesMax, v)) return false;
      s.pulses[s.lane] = static_cast<std::uint8_t>(v);
      return true;
    case 3:
      if (!int_in(value, kLengthMin, kLengthMax, v)) return false;
      s.lengths[s.lane] = static_cast<std::uint8_t>(v);
      // Source set_length: pulses above the new length are clamped (and the UI shows it).
      if (s.pulses[s.lane] > v) {
        s.pulses[s.lane] = static_cast<std::uint8_t>(v);
        effects = EffectRepaint;
      }
      return true;
    case 4:
      if (!int_in(value, -kRotationMax, kRotationMax, v)) return false;
      s.rotations[s.lane] = v;
      return true;
    case 5:
      if (!int_in(value, 0, 2, v)) return false;
      s.sort_mode = static_cast<std::uint8_t>(v);
      return true;
    case 6:
      if (!decimal_in(value, f)) return false;
      s.gate = f;
      return true;
    case 7:
      if (!int_in(value, 0, 2, v)) return false;
      s.retrigger_mode = static_cast<std::uint8_t>(v);
      return true;
    default:
      return false;
  }
}

void euclid_attributes(ModuleState& state, AttributeVisitor& v) noexcept {
  if (!v.visit) return;
  auto& s = state.storage.euclid;
  AttributeDescriptor attrs[3] = {
      {"pulses", AttributeType::Byte, static_cast<std::uint16_t>(kEuclidLanes), s.pulses},
      {"lengths", AttributeType::Byte, static_cast<std::uint16_t>(kEuclidLanes), s.lengths},
      {"rotations", AttributeType::Integer, static_cast<std::uint16_t>(kEuclidLanes), s.rotations}};
  for (auto& attr : attrs) {
    if (!v.visit(v.ctx, attr)) break;
  }
}

const ModuleDescriptor& euclid_module_descriptor() noexcept { return kEuclidDescriptor; }
}  // namespace orbit
