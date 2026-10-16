#include "arp.hpp"
#include "drop_randomize.hpp"
#include "../music.hpp"
#include "../time.hpp"
#include <cstdint>
#include <limits>
#include <new>

// Deviations from circuitpython-source/src/apps/Orion/modules/arp.py (each is also
// listed in the t09e report):
//  1. An emission is all-or-nothing. The source sends the note-on and drops its scheduled
//     off when the 12-entry schedule is full, which leaves a stuck note. Here the note-on is
//     refused and counted (schedule_overflow). Due releases are popped before new notes are
//     generated, so a slot freed by a due release is reused in the same pass. Output differs from
//     the source only in passes where the source's schedule overflows (its stuck-note case).
//  2. Pattern shift is int32 (the source's RYSHFT is unbounded). Gate is float32; int() truncation
//     is applied at use, as in the source.
//  3. The grid index is taken from the uint32 transport tick. A wrap restarts the grid like a
//     backwards tick (one trigger), where the source's tick is unbounded.
//  4. Scheduled releases are discarded on stop/scene reset without being sent, as in the source.
//     Chain::reset does not release sounding notes. The controller must call Output::all_notes_off
//     on Stop/SceneChange, as the source's stop_all_modules does; otherwise these notes stick.
// Exact, not deviations:
//  - Trigger batches (grid_change returns current_bin - last_bin) are fast-forwarded in closed form
//    once no emission is possible. The source loops per trigger; emissions stop at output or
//    schedule capacity, and the remaining triggers only change position and step counters.
//  - Transpose degrees are clamped to +-4096 before music::transpose. Any |degree| beyond about 128
//    saturates the note to 0 or 127, so the result is unchanged.
//  - Hash-based random selection uses seeded::random_int, which is exact for the source's
//    unbounded integer hash (see drop_randomize.cpp).

namespace orbit {
namespace {
constexpr std::uint8_t kSortDown = 1;
constexpr std::uint8_t kPlayMirror = 1;
constexpr std::uint8_t kPlayThumb = 2;
constexpr std::uint8_t kPlayPinky = 3;
constexpr std::uint8_t kPlayDiverge = 4;
constexpr std::uint8_t kPlayConverge = 5;
constexpr std::uint8_t kPlayConDiverge = 6;
constexpr std::uint8_t kPlayRandom = 7;
constexpr std::uint8_t kPlayRepeat = 8;
constexpr std::uint8_t kBoundaryWrapUni = 0;
constexpr std::uint8_t kBoundaryWrapBi = 1;
constexpr std::uint8_t kBoundaryFoldUni = 2;
constexpr std::uint8_t kBoundaryClamp = 4;
constexpr std::uint8_t kRetriggerRtrg = 0;
constexpr std::uint8_t kRetriggerStable = 2;
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr float kGateMax = 100000.0f;
constexpr std::int64_t kTransposeDegreeLimit = 4096;
// Explicit trigger iterations per pass. With a non-empty pattern an emission happens at least
// every 8 triggers, so 128 iterations always fill the output or schedule when emission is
// possible; the remainder is then fast-forwarded exactly.
constexpr std::uint64_t kMaxExplicitTriggers = 128;
constexpr auto kCapacity = static_cast<std::uint16_t>(kPolyphony);
constexpr std::uint8_t kCapacityByte = static_cast<std::uint8_t>(kPolyphony);

constexpr const char* kRateLabel = "RATE";
constexpr const char* kSortLabel = "SORT";
constexpr const char* kPlayLabel = "PLAY";
constexpr const char* kTransposeLabel = "TRNS";
constexpr const char* kMaxTransposeLabel = "MAXTRN";
constexpr const char* kBoundaryLabel = "BND";
constexpr const char* kGateLabel = "GATE";
constexpr const char* kPatternLabel = "RYTM";
constexpr const char* kPatternShiftLabel = "RYSHFT";
constexpr const char* kRandomLengthLabel = "RNDLEN";
constexpr const char* kSeedLabel = "SEED";
constexpr const char* kRetriggerLabel = "RTRG";

constexpr const char* kSortOptions[] = {"UP", "DOWN", "ORD"};
constexpr const char* kPlayOptions[] = {"SORT", "MIRR", "THUMB", "PINKY", "DIV", "CONV", "CDIV", "RND", "RPT"};
constexpr const char* kBoundaryOptions[] = {"WRPU", "WRPB", "FLDU", "FLDB", "CLMP"};
constexpr const char* kRetriggerOptions[] = {"RTRG", "CONT", "STBL"};

const ParamSpec kArpParams[] = {
    {"rate", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(12), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
    {"sort_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kSortLabel, kSortOptions, 3, ParamPersist},
    {"play_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(8), ParamValue::integer(1), kPlayLabel, kPlayOptions, 9, ParamPersist},
    {"transpose_steps", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(-24), ParamValue::integer(24), ParamValue::integer(1), kTransposeLabel, nullptr, 0, ParamPersist},
    {"max_transpose", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(48), ParamValue::integer(1), kMaxTransposeLabel, nullptr, 0, ParamPersist},
    {"boundary_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(4), ParamValue::integer(1), kBoundaryLabel, kBoundaryOptions, 5, ParamPersist},
    {"gate", ParamType::Decimal, ParamValue::decimal(100.0f), ParamValue::decimal(0.0f), ParamValue::decimal(kGateMax), ParamValue::decimal(5.0f), kGateLabel, nullptr, 0, ParamPersist},
    {"pattern", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(24), ParamValue::integer(1), kPatternLabel, nullptr, 0, ParamPersist},
    {"pattern_shift", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kPatternShiftLabel, nullptr, 0, ParamPersist},
    {"random_pattern_length", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(128), ParamValue::integer(1), kRandomLengthLabel, nullptr, 0, ParamPersist},
    {"rand_seed", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(10000), ParamValue::integer(1), kSeedLabel, nullptr, 0, ParamPersist},
    {"retrigger_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kRetriggerLabel, kRetriggerOptions, 3, ParamPersist},
};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kArpDescriptor{"arp", 1, kArpParams, 12, construct_arp_state, descriptor_destroy, process_arp, reset_arp_state, set_arp_param, attr_none};

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

// Python floor modulo for a positive modulus.
std::int64_t floor_mod(std::int64_t value, std::int64_t modulus) noexcept {
  const auto remainder = value % modulus;
  return remainder < 0 ? remainder + modulus : remainder;
}

// get_pattern_step(bits, length, position + shift): MSB-first bit order, floor modulo.
bool pattern_on(const ArpModuleState& s, std::int64_t position) noexcept {
  const auto length = static_cast<std::int64_t>(music::kPatternLengths[s.pattern]);
  const auto bits = music::kPatternBits[s.pattern];
  const auto index = floor_mod(position + s.pattern_shift, length);
  return ((bits >> (length - index - 1)) & 1U) != 0;
}

// Source BOUNDARY modes (utils.bound_offset). Limit <= 0 leaves the offset unchanged.
std::int64_t bound_offset(std::int64_t offset, std::int32_t limit, std::uint8_t mode) noexcept {
  if (limit <= 0) return offset;
  const auto lim = static_cast<std::int64_t>(limit);
  if (mode == kBoundaryClamp) return offset < -lim ? -lim : (offset > lim ? lim : offset);
  if (mode == kBoundaryWrapUni) return floor_mod(offset, lim + 1);
  if (mode == kBoundaryWrapBi) return floor_mod(offset + lim, lim * 2 + 1) - lim;
  if (mode == kBoundaryFoldUni) {
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

std::uint8_t transpose_pitch(const ArpModuleState& s, const ProcessContext& context, std::uint8_t pitch,
                             std::uint64_t completion) noexcept {
  const auto raw = static_cast<std::int64_t>(s.transpose_steps) * static_cast<std::int64_t>(completion);
  const auto steps = bound_offset(raw, s.max_transpose, s.boundary_mode);
  if (steps == 0) return pitch;
  std::int64_t degrees = steps;
  if (degrees > kTransposeDegreeLimit) degrees = kTransposeDegreeLimit;
  if (degrees < -kTransposeDegreeLimit) degrees = -kTransposeDegreeLimit;
  return music::transpose(pitch, static_cast<std::int32_t>(degrees), 0, true, context.global.key, context.global.scale);
}

// Source Arp.path_length.
std::uint64_t path_length(const ArpModuleState& s, std::uint64_t count) noexcept {
  if (count < 2) return 1;
  if (s.play_mode == kPlayMirror || s.play_mode == kPlayThumb || s.play_mode == kPlayPinky) return count * 2 - 2;
  if (s.play_mode == kPlayConDiverge) return count * 2 - 1;
  return count;
}

// Source Arp.diverge_index. Positions are always within 0..count-1 for the callers below.
std::int64_t diverge_index(std::int64_t position, std::int64_t count) noexcept {
  const auto center = (count - 1) / 2;
  if (position == 0) return center;
  const auto step = (position + 1) / 2;
  if (position % 2 != 0) return center + step;
  return center - step;
}

// Source Arp.path_index for non-random modes.
std::uint64_t path_index(const ArpModuleState& s, std::uint64_t position, std::uint64_t count) noexcept {
  if (count < 2) return 0;
  const auto length = path_length(s, count);
  const auto p = position % length;
  const auto c = static_cast<std::int64_t>(count);
  const auto q = static_cast<std::int64_t>(p);
  std::int64_t index = 0;
  switch (s.play_mode) {
    case kPlayMirror:
      return p < count ? p : length - p;
    case kPlayThumb:
      return p % 2 == 0 ? 0 : (p + 1) / 2;
    case kPlayPinky:
      return p % 2 == 0 ? count - 1 : (p - 1) / 2;
    case kPlayDiverge:
      index = diverge_index(q, c);
      break;
    case kPlayConverge:
      index = diverge_index(c - 1 - q, c);
      break;
    case kPlayConDiverge:
      index = p < count ? diverge_index(c - 1 - q, c) : diverge_index(q - c + 1, c);
      break;
    default:
      return p % count;
  }
  return static_cast<std::uint64_t>(floor_mod(index, c));
}

// Source Arp.generate_notes, stable-mode step recomputation for non-repeat modes.
std::uint64_t stable_base(const ArpModuleState& s, std::uint64_t timing_tick) noexcept {
  const auto length = static_cast<std::uint64_t>(music::kPatternLengths[s.pattern]);
  const auto bits = music::kPatternBits[s.pattern];
  const auto iteration = timing_tick / length;
  const auto current = timing_tick % length;
  const auto empty = static_cast<std::uint64_t>(music::pattern_empty_steps(bits, static_cast<std::uint8_t>(length)));
  std::uint64_t past = iteration * (length - empty);
  for (std::uint64_t i = 0; i < current; ++i) {
    if (pattern_on(s, static_cast<std::int64_t>(i))) ++past;
  }
  return past;
}

std::uint32_t gate_ms(const ArpModuleState& s) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(s.gate));
}

std::uint32_t rate_ticks(const ArpModuleState& s) noexcept {
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

// Source Arp.update_note_register.
void update_notes(ArpModuleState& s, const NoteBatch& input) noexcept {
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
  if (s.sort_mode == 0 || s.play_mode == kPlayRandom) {
    sort_ascending(s.notes);
  } else if (s.sort_mode == kSortDown) {
    sort_ascending(s.notes);
    reverse_notes(s.notes);
  }
}

// Due releases leave in schedule order, before this pass generates new notes. New entries
// are due at now + gate >= now, so the strict comparison never releases them in the same pass.
void pop_due_offs(ArpModuleState& s, std::uint32_t now, NoteBatch& output) noexcept {
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

bool can_emit(const ArpModuleState& s, const NoteBatch& output) noexcept {
  return s.scheduled_count < kCapacityByte && output.on.count < kCapacity;
}

// Deviation 1: the note and its release are queued together or not at all.
void emit(ArpModuleState& s, NoteBatch& output, const Note& note, std::uint32_t due) noexcept {
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

// One source generate_notes call.
void generate_one(ArpModuleState& s, const ProcessContext& context, NoteBatch& output) noexcept {
  const auto now = context.now.ms;
  const auto gate = gate_ms(s);
  const auto timing_tick = static_cast<std::uint64_t>(context.transport.tick / rate_ticks(s));
  std::uint64_t random_time = timing_tick;
  if (s.random_pattern_length != 0) {
    random_time = timing_tick % static_cast<std::uint64_t>(s.random_pattern_length);
  }
  if (s.retrigger_mode == kRetriggerStable) s.step = timing_tick;
  const bool check = pattern_on(s, static_cast<std::int64_t>(s.step));
  ++s.step;
  if (!check) return;

  const auto count = static_cast<std::uint16_t>(s.notes.count);
  if (s.play_mode == kPlayRepeat) {
    const auto completion = s.position;
    for (std::uint16_t i = 0; i < count; ++i) {
      const Note& held = s.notes.items[i];
      const Note note{transpose_pitch(s, context, held.pitch, completion), held.velocity, held.channel};
      emit(s, output, note, add_deadline(now, gate));
    }
    ++s.position;
    return;
  }

  if (count == 0) return;  // Unreachable: arp_state requires a non-empty register.
  if (s.retrigger_mode == kRetriggerStable) s.position = stable_base(s, timing_tick);
  const auto completion = s.position / path_length(s, count);
  std::uint16_t index = 0;
  if (s.play_mode == kPlayRandom) {
    const auto seed = random_time + static_cast<std::uint64_t>(s.random_seed_user);
    index = static_cast<std::uint16_t>(seeded::random_int(seed, static_cast<std::int64_t>(count) - 1, 0));
  } else {
    index = static_cast<std::uint16_t>(path_index(s, s.position, count));
  }
  const Note& held = s.notes.items[index];
  const Note note{transpose_pitch(s, context, held.pitch, completion), held.velocity, held.channel};
  emit(s, output, note, add_deadline(now, gate));
  ++s.position;
}

// Number of pattern hits over `count` consecutive steps starting at `start`.
std::uint64_t ones_in_window(const ArpModuleState& s, std::uint64_t start, std::uint64_t count) noexcept {
  const auto length = static_cast<std::uint64_t>(music::kPatternLengths[s.pattern]);
  std::uint64_t full = 0;
  for (std::uint64_t x = 0; x < length; ++x) {
    if (pattern_on(s, static_cast<std::int64_t>(start + x))) ++full;
  }
  std::uint64_t ones = (count / length) * full;
  const auto remainder = count % length;
  for (std::uint64_t x = 0; x < remainder; ++x) {
    if (pattern_on(s, static_cast<std::int64_t>(start + x))) ++ones;
  }
  return ones;
}

// State after `triggers` generate_one calls that emit nothing (output or schedule full, or
// stable mode with no hit). Matches the per-trigger updates of generate_one exactly.
void fast_forward(ArpModuleState& s, const ProcessContext& context, std::uint64_t triggers) noexcept {
  if (s.retrigger_mode == kRetriggerStable) {
    const auto timing_tick = static_cast<std::uint64_t>(context.transport.tick / rate_ticks(s));
    const bool check = pattern_on(s, static_cast<std::int64_t>(timing_tick));
    s.step = timing_tick + 1;
    if (s.play_mode == kPlayRepeat) {
      if (check) s.position += triggers;
    } else if (check) {
      s.position = stable_base(s, timing_tick) + 1;
    }
    return;
  }
  s.position += ones_in_window(s, s.step, triggers);
  s.step += triggers;
}

// Source Arp.process grid loop: `triggers` generate_notes calls, bounded work.
void generate_triggers(ArpModuleState& s, const ProcessContext& context, NoteBatch& output,
                       std::uint64_t triggers) noexcept {
  std::uint64_t done = 0;
  while (done < triggers && done < kMaxExplicitTriggers && can_emit(s, output)) {
    generate_one(s, context, output);
    ++done;
  }
  if (done < triggers) fast_forward(s, context, triggers - done);
}

}  // namespace

void construct_arp_state(ModuleState& state) noexcept { new (&state.storage.arp) ArpModuleState{}; }

// Source Arp.process.
void process_arp(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.arp;
  output = {};
  const bool running = context.transport.running;
  pop_due_offs(s, context.now.ms, output);

  std::uint16_t counter = 0;
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    if (has_pitch(s.notes, input.off.items[i].pitch)) ++counter;
  }
  const bool same_frame_retrigger = counter == s.notes.count;

  if (input.on.count > 0 || input.off.count > 0) update_notes(s, input);

  if ((s.arp_state == 1 && s.notes.count < 1) || same_frame_retrigger) {
    s.arp_state = 0;
    if (s.retrigger_mode == kRetriggerRtrg) {
      s.position = 0;
      s.step = 0;
    }
  }
  if (s.arp_state == 0 && s.notes.count > 0) s.arp_state = 1;

  if (running && !s.was_running) {
    s.last_grid_bin = -1;
    if (s.retrigger_mode == kRetriggerRtrg) {
      s.position = 0;
      s.step = 0;
    }
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

// Source Arp.stop. Pending releases are dropped; the controller releases sounding notes.
void reset_arp_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.arp;
  s.held.clear();
  s.notes.clear();
  s.scheduled_count = 0;
  s.arp_state = 0;
  s.position = 0;
  s.step = 0;
  s.last_grid_bin = -1;
  s.was_running = false;
}

bool set_arp_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = 0;
  auto& s = state.storage.arp;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, 0, 12, v)) return false; s.rate_index = static_cast<std::uint8_t>(v); return true;
    case 1: if (!int_in(value, 0, 2, v)) return false; s.sort_mode = static_cast<std::uint8_t>(v); return true;
    case 2: if (!int_in(value, 0, 8, v)) return false; s.play_mode = static_cast<std::uint8_t>(v); return true;
    case 3: if (!int_in(value, -24, 24, v)) return false; s.transpose_steps = v; return true;
    case 4: if (!int_in(value, 0, 48, v)) return false; s.max_transpose = v; return true;
    case 5: if (!int_in(value, 0, 4, v)) return false; s.boundary_mode = static_cast<std::uint8_t>(v); return true;
    case 6:
      if (value.type != ParamType::Decimal || !(value.value.decimal >= 0.0f && value.value.decimal <= kGateMax)) return false;
      s.gate = value.value.decimal;
      return true;
    case 7: if (!int_in(value, 0, 24, v)) return false; s.pattern = static_cast<std::uint8_t>(v); return true;
    case 8: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.pattern_shift = v; return true;
    case 9: if (!int_in(value, 0, 128, v)) return false; s.random_pattern_length = v; return true;
    case 10: if (!int_in(value, 0, 10000, v)) return false; s.random_seed_user = v; return true;
    case 11: if (!int_in(value, 0, 2, v)) return false; s.retrigger_mode = static_cast<std::uint8_t>(v); return true;
    default: return false;
  }
}

const ModuleDescriptor& arp_module_descriptor() noexcept { return kArpDescriptor; }
}  // namespace orbit
