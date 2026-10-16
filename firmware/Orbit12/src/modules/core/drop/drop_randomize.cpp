#include "drop_randomize.hpp"
#include "../../../domain/music.hpp"
#include <cstdint>
#include <limits>
#include <new>

namespace orbit {
namespace seeded {
std::uint32_t hash_u32(std::uint64_t value) noexcept {
  value = (value ^ 61U) ^ (value >> 16U);
  value = value + (value << 3U);
  value = value ^ (value >> 4U);
  value = value * 0x27D4EB2DU;
  value = value ^ (value >> 15U);
  return static_cast<std::uint32_t>(value & 0xFFFFFFFFU);
}

double random_float(std::uint64_t seed) noexcept {
  return static_cast<double>(hash_u32(seed)) / 4294967296.0;
}

std::int64_t random_int(std::uint64_t seed, std::int64_t max_value, std::uint8_t mode) noexcept {
  std::int64_t low = 0;
  std::int64_t high = 0;
  if (mode == 0) {
    low = max_value < 0 ? max_value : 0;
    high = max_value > 0 ? max_value : 0;
  } else {
    const std::int64_t magnitude = max_value < 0 ? -max_value : max_value;
    low = -magnitude;
    high = magnitude;
  }
  const auto span = static_cast<std::uint64_t>(high - low + 1);
  return low + static_cast<std::int64_t>(hash_u32(seed) % span);
}
}  // namespace seeded

namespace {
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr std::uint8_t kNone = 0;
constexpr const char* kDropLabel = "DROP";
constexpr const char* kDropModeLabel = "MDE";
constexpr const char* kPatternLengthLabel = "PTLEN";
constexpr const char* kSeedLabel = "SEED";
constexpr const char* kDropModeOptions[] = {"NOTE", "STEP"};
constexpr const char* kRandomizeModeOptions[] = {"UNI", "BI"};

const ParamSpec kDropParams[] = {
    {"drop", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(100), ParamValue::integer(5), kDropLabel, nullptr, 0, ParamPersist},
    {"mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), kDropModeLabel, kDropModeOptions, 2, ParamPersist},
    {"pattern_length", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(128), ParamValue::integer(1), kPatternLengthLabel, nullptr, 0, ParamPersist},
    {"seed", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(10000), ParamValue::integer(1), kSeedLabel, nullptr, 0, ParamPersist}};

const ParamSpec kRandomizeParams[] = {
    {"velocity", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), "VELRNG", nullptr, 0, ParamPersist},
    {"velocity_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), "VELMDE", kRandomizeModeOptions, 2, ParamPersist},
    {"note", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), "NTRNG", nullptr, 0, ParamPersist},
    {"note_increment", ParamType::Integer, ParamValue::integer(1), ParamValue::integer(1), ParamValue::integer(128), ParamValue::integer(1), "NTINC", nullptr, 0, ParamPersist},
    {"note_chance", ParamType::Decimal, ParamValue::decimal(0.5f), ParamValue::decimal(0.0f), ParamValue::decimal(1.0f), ParamValue::decimal(0.05f), "NTCHNC", nullptr, 0, ParamPersist},
    {"note_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), "NTMDE", kRandomizeModeOptions, 2, ParamPersist},
    {"scale_aware", ParamType::Boolean, ParamValue::boolean(true), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::integer(1), "INSCL", nullptr, 0, ParamPersist},
    {"octave", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), "OCRNG", nullptr, 0, ParamPersist},
    {"octave_chance", ParamType::Decimal, ParamValue::decimal(0.5f), ParamValue::decimal(0.0f), ParamValue::decimal(1.0f), ParamValue::decimal(0.05f), "OCCHNC", nullptr, 0, ParamPersist},
    {"octave_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), "OCMDE", kRandomizeModeOptions, 2, ParamPersist},
    {"pattern_length", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(128), ParamValue::integer(1), kPatternLengthLabel, nullptr, 0, ParamPersist},
    {"random_seed_user", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(10000), ParamValue::integer(1), kSeedLabel, nullptr, 0, ParamPersist}};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kDropDescriptor{"drop", 1, kDropParams, 4, construct_drop_state, descriptor_destroy, process_drop, reset_drop_state, set_drop_param, attr_none};
const ModuleDescriptor kRandomizeDescriptor{"randomize", 1, kRandomizeParams, 12, construct_randomize_state, descriptor_destroy, process_randomize, reset_randomize_state, set_randomize_param, attr_none};

std::uint8_t clamp_midi(std::int64_t value) noexcept {
  if (value < 0) return 0;
  if (value > kMidiMax) return kMidiMax;
  return static_cast<std::uint8_t>(value);
}

bool int_in(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer && value.type != ParamType::Enumeration) return false;
  if (value.value.integer < low || value.value.integer > high) return false;
  out = value.value.integer;
  return true;
}

bool chance_in(const ParamValue& value, float& out) noexcept {
  if (value.type != ParamType::Decimal) return false;
  if (!(value.value.decimal >= 0.0f && value.value.decimal <= 1.0f)) return false;
  out = value.value.decimal;
  return true;
}
}  // namespace

void construct_drop_state(ModuleState& state) noexcept { new (&state.storage.drop) DropModuleState{}; }
void construct_randomize_state(ModuleState& state) noexcept { new (&state.storage.randomize) RandomizeModuleState{}; }

// Source Drop.process: ons are dropped per note (MDE=NOTE, seed+index) or per
// step (MDE=STEP). All note-offs pass through unchanged, as in the source.
void process_drop(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  const auto& s = state.storage.drop;
  output = {};
  auto step = static_cast<std::uint64_t>(context.transport.tick / 6U);
  if (s.pattern_length != 0) step %= static_cast<std::uint64_t>(s.pattern_length);
  const auto seed_base = step + static_cast<std::uint64_t>(s.seed);
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const auto seed = s.mode == 1 ? seed_base : seed_base + i;
    const bool drop = s.drop >= 100 ||
                      (s.drop > 0 && seeded::random_float(seed) < static_cast<double>(s.drop) / 100.0);
    if (!drop) (void)output.on.append(input.on.items[i]);
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) (void)output.off.append(input.off.items[i]);
}

// Source Randomize.process. Deviation for release safety: an on whose mapping
// cannot be recorded (relationship table full) is refused and counted. The
// source would emit it with no mapping, so its off could never be sent.
void process_randomize(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.randomize;
  output = {};
  const auto tick = static_cast<std::uint64_t>(context.transport.tick);
  const auto seed_base = s.pattern_length != 0
                             ? tick % (static_cast<std::uint64_t>(s.pattern_length) * 6U)
                             : tick;
  const auto seed_root = seed_base + static_cast<std::uint64_t>(s.random_seed_user);
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const auto& note = input.on.items[i];
    const auto seed = seed_root + i;
    std::int64_t note_offset = 0;
    if (s.note_range != 0 && s.note_chance != 0.0f &&
        seeded::random_float(seed + 100U) < static_cast<double>(s.note_chance)) {
      note_offset = seeded::random_int(seed + 200U, s.note_range, s.note_mode) * s.note_increment;
    }
    std::int64_t octave_offset = 0;
    if (s.octave_range != 0 && s.octave_chance != 0.0f &&
        seeded::random_float(seed + 300U) < static_cast<double>(s.octave_chance)) {
      octave_offset = seeded::random_int(seed + 400U, s.octave_range, s.octave_mode);
    }
    std::int64_t velocity_offset = 0;
    if (s.velocity_range != 0) {
      velocity_offset = seeded::random_int(seed + 600U, s.velocity_range, s.velocity_mode);
    }

    std::uint8_t new_note = 0;
    if (note_offset != 0) {
      // Degrees beyond int32 are saturated before music::transpose. Its output
      // is clamped to MIDI, so this only avoids signed overflow at the boundary.
      std::int64_t degrees = note_offset;
      if (degrees > kIntMax) degrees = kIntMax;
      if (degrees < kIntMin) degrees = kIntMin;
      new_note = music::transpose(note.pitch, static_cast<std::int32_t>(degrees),
                                  static_cast<std::int32_t>(octave_offset), s.scale_aware,
                                  context.global.key, context.global.scale);
    } else {
      // The source adds octaves without clamping, and its bytearray would raise
      // on out-of-range values. Clamp to MIDI, as music::transpose does.
      new_note = clamp_midi(static_cast<std::int64_t>(note.pitch) + octave_offset * 12);
    }
    if (s.relationships.count >= NoteRelationships::kCapacity) {
      ++s.relationship_overflow;
      continue;
    }
    const auto velocity = clamp_midi(static_cast<std::int64_t>(note.velocity) + velocity_offset);
    if (output.on.append({new_note, velocity, note.channel}) != PushResult::Added) {
      ++s.relationship_overflow;
      continue;
    }
    (void)s.relationships.add(note.pitch, new_note);
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const auto& note = input.off.items[i];
    std::uint8_t mapped = 0;
    if (s.relationships.remove_first(note.pitch, mapped)) (void)output.off.append({mapped, 0, note.channel});
  }
}

void reset_drop_state(ModuleState&, ResetReason) noexcept {}

void reset_randomize_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic || reason == ResetReason::Init)
    state.storage.randomize.relationships.clear();
}

bool set_drop_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.drop;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, 0, 100, v)) return false; s.drop = v; return true;
    case 1: if (!int_in(value, 0, 1, v)) return false; s.mode = static_cast<std::uint8_t>(v); return true;
    case 2: if (!int_in(value, 0, 128, v)) return false; s.pattern_length = v; return true;
    case 3: if (!int_in(value, 0, 10000, v)) return false; s.seed = v; return true;
    default: return false;
  }
}

bool set_randomize_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.randomize;
  std::int32_t v = 0;
  float f = 0.0f;
  switch (index) {
    case 0: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.velocity_range = v; return true;
    case 1: if (!int_in(value, 0, 1, v)) return false; s.velocity_mode = static_cast<std::uint8_t>(v); return true;
    case 2: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.note_range = v; return true;
    case 3: if (!int_in(value, 1, 128, v)) return false; s.note_increment = v; return true;
    case 4: if (!chance_in(value, f)) return false; s.note_chance = f; return true;
    case 5: if (!int_in(value, 0, 1, v)) return false; s.note_mode = static_cast<std::uint8_t>(v); return true;
    case 6:
      if (value.type != ParamType::Boolean) return false;
      s.scale_aware = value.value.boolean;
      return true;
    case 7: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.octave_range = v; return true;
    case 8: if (!chance_in(value, f)) return false; s.octave_chance = f; return true;
    case 9: if (!int_in(value, 0, 1, v)) return false; s.octave_mode = static_cast<std::uint8_t>(v); return true;
    case 10: if (!int_in(value, 0, 128, v)) return false; s.pattern_length = v; return true;
    case 11: if (!int_in(value, 0, 10000, v)) return false; s.random_seed_user = v; return true;
    default: return false;
  }
}

const ModuleDescriptor& drop_module_descriptor() noexcept { return kDropDescriptor; }
const ModuleDescriptor& randomize_module_descriptor() noexcept { return kRandomizeDescriptor; }
}  // namespace orbit
