#include "quantize_strum.hpp"
#include "../drop/drop_randomize.hpp"
#include "../../../domain/music.hpp"
#include <cstdint>
#include <limits>
#include <new>

// Deviations from circuitpython-source/src/apps/Orion/modules/quantize.py and strum.py
// (each is also listed in the t09i report):
//  Q1. A note-off that does not fit the 12-entry output batch is flagged through the batch's
//      sticky overflow (the chain then releases everything and requires a restart). The source
//      silently drops it, which leaves a stuck note.
//  Q2. The transport tick is unwrapped to 64 bits from 32-bit deltas. The source's midi_tick is
//      unbounded, so grid alignment is identical for any sequence the source can represent.
//  Q3. A release is never due before its pending note-on is promoted (clamped to the pending due
//      tick). Monotonic ticks never reach the clamp. After a backwards tick restart (the source's
//      Start in external mode), the source can schedule a release before promotion and leave the
//      note sounding; the clamp releases it after promotion instead.
//  S1. A pitch present in both the on and off lists of one batch is removed from the strum
//      register. The source keeps it and later emits a note-on whose release was already
//      forwarded, which sticks. The chain cannot distinguish this from a re-press in the same
//      batch, so this trades that strum onset for release safety.
//  S2. Parameter ranges: ORDR is 0..4 (the source's minmax admits 5, which has no option name;
//      its display raises IndexError). A value of 5 is rejected.
//  S3. tilt is stored as float32 (ParamValue carries float). The source keeps a double; traces
//      round tilt to float32 before applying it so both sides compute with the same value.
// Exact source behavior that looks wrong and is preserved (see the t09i report):
//  - CNT (tilt_note_count) is stored but never used: the source passes STRUM_CHORD_LENGTH (5).
//  - ARTC=EVEN (0) indexes STRUM_ARTICULATE_PATTERNS[-3], which Python resolves to the BNC pattern;
//    the labels HUMN/RAKE/SWNG/FLAM map to patterns one position earlier than their labels suggest.
//  - Strum stop() clears the register only; strum_active, the schedule and strum_count persist.
//  - Quantize and strum drop note-ons when their arrays are full (counted, never stuck).

namespace orbit {
namespace {

constexpr std::uint8_t kQuantizeRateDefault = 8;
constexpr std::uint8_t kRateMaxIndex = 12;
constexpr std::int32_t kQuantizeIndexMax = 12;

constexpr std::uint8_t kOrderUp = 1;
constexpr std::uint8_t kOrderDown = 2;
constexpr std::uint8_t kOrderUpDown = 3;
constexpr std::uint8_t kOrderRandom = 4;
constexpr std::int32_t kOrderMax = 4;
constexpr std::int32_t kAmountMax = 1000000;
constexpr std::int32_t kChordCountMax = 12;
constexpr std::int32_t kArticulateMax = 8;
constexpr std::int32_t kStrumChordLength = 5;  // Source passes STRUM_CHORD_LENGTH to the tilt.
constexpr std::uint8_t kStrumChordLengthByte = 5;
constexpr std::uint8_t kPatternSteps = 4;
constexpr std::uint8_t kPatternCount = 6;

// STRUM_ARTICULATE_PATTERNS, in source order (index = ARTC - 3, Python negative indexing).
constexpr std::uint8_t kArticulationPatterns[kPatternCount][kPatternSteps] = {
    {24, 16, 8, 4}, {4, 8, 16, 24}, {28, 4, 18, 5}, {4, 22, 5, 20}, {18, 5, 20, 6}, {32, 8, 5, 4}};

constexpr std::uint8_t kCapacityByte = static_cast<std::uint8_t>(kPolyphony);

constexpr const char* kRateLabel = "RATE";
constexpr const char* kAmountLabel = "AMT";
constexpr const char* kOrderLabel = "ORDR";
constexpr const char* kTiltLabel = "TILT";
constexpr const char* kCountLabel = "CNT";
constexpr const char* kArticulateLabel = "ARTC";

constexpr const char* kOrderOptions[] = {"ORD", "UP", "DOWN", "UPDWN", "RAND"};
constexpr const char* kArticulateOptions[] = {"EVEN", "RNDLTE", "RNDSTR", "ACCL", "DECL",
                                              "HUMN", "RAKE",   "SWNG",   "FLAM"};

const ParamSpec kQuantizeParams[] = {
    {"rate", ParamType::Integer, ParamValue::integer(kQuantizeRateDefault), ParamValue::integer(0),
     ParamValue::integer(kRateMaxIndex), ParamValue::integer(1), kRateLabel, nullptr, 0, ParamPersist},
};

const ParamSpec kStrumParams[] = {
    {"amount", ParamType::Integer, ParamValue::integer(30), ParamValue::integer(0),
     ParamValue::integer(kAmountMax), ParamValue::integer(1), kAmountLabel, nullptr, 0, ParamPersist},
    {"order", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0),
     ParamValue::integer(kOrderMax), ParamValue::integer(1), kOrderLabel, kOrderOptions, 5, ParamPersist},
    {"tilt", ParamType::Decimal, ParamValue::decimal(0.0f), ParamValue::decimal(-1.0f),
     ParamValue::decimal(1.0f), ParamValue::decimal(0.05f), kTiltLabel, nullptr, 0, ParamPersist},
    {"tilt_note_count", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(1),
     ParamValue::integer(kChordCountMax), ParamValue::integer(1), kCountLabel, nullptr, 0, ParamPersist},
    {"articulate", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0),
     ParamValue::integer(kArticulateMax), ParamValue::integer(1), kArticulateLabel, kArticulateOptions, 9,
     ParamPersist},
};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kQuantizeDescriptor{"quantize", 1, kQuantizeParams, 1, construct_quantize_state,
                                           descriptor_destroy, process_quantize, reset_quantize_state,
                                           set_quantize_param, attr_none};
const ModuleDescriptor kStrumDescriptor{"strum", 1, kStrumParams, 5, construct_strum_state,
                                        descriptor_destroy, process_strum, reset_strum_state,
                                        set_strum_param, attr_none};

bool int_in(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer && value.type != ParamType::Enumeration) return false;
  if (value.value.integer < low || value.value.integer > high) return false;
  out = value.value.integer;
  return true;
}

// Floor division for a positive divisor (Python //).
std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  auto quotient = value / divisor;
  if (value % divisor < 0) --quotient;
  return quotient;
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

void remove_index(NoteBuffer& buffer, std::uint16_t index) noexcept {
  for (auto j = static_cast<std::uint16_t>(index + 1); j < buffer.count; ++j) buffer.items[j - 1] = buffer.items[j];
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

// NoteArray.reverse_notes.
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

// ---------------------------------------------------------------- quantize

// Source Quantize.note_delays.times: 1..96 MIDI ticks.
std::uint8_t clamp_delay(std::int64_t delay) noexcept {
  return static_cast<std::uint8_t>(delay < 0 ? 0 : (delay > 255 ? 255 : delay));
}

// Index of the first entry with this pitch, or -1.
template <typename Entry>
int find_pending(const Entry* entries, std::uint8_t count, std::uint8_t pitch) noexcept {
  for (std::uint8_t i = 0; i < count; ++i) {
    if (entries[i].note.pitch == pitch) return static_cast<int>(i);
  }
  return -1;
}

int find_delay(const QuantizeModuleState& s, std::uint8_t pitch) noexcept {
  for (std::uint8_t i = 0; i < s.delay_count; ++i) {
    if (s.delays[i].pitch == pitch) return static_cast<int>(i);
  }
  return -1;
}

void remove_delay(QuantizeModuleState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = static_cast<std::uint8_t>(index + 1); i < s.delay_count; ++i) s.delays[i - 1] = s.delays[i];
  --s.delay_count;
}

void remove_pending(QuantizeModuleState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = static_cast<std::uint8_t>(index + 1); i < s.pending_count; ++i) s.pending[i - 1] = s.pending[i];
  --s.pending_count;
}

void remove_scheduled(QuantizeModuleState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = static_cast<std::uint8_t>(index + 1); i < s.scheduled_count; ++i) s.scheduled[i - 1] = s.scheduled[i];
  --s.scheduled_count;
}

// Source note_delays: update the first record for the pitch, otherwise append it.
void record_delay(QuantizeModuleState& s, std::uint8_t pitch, std::int64_t delay) noexcept {
  const int index = find_delay(s, pitch);
  if (index >= 0) {
    s.delays[index].delay = clamp_delay(delay);
    return;
  }
  if (s.delay_count >= kCapacityByte) {
    ++s.delay_unrecorded;  // Source drops the record silently; the off then uses the default delay.
    return;
  }
  s.delays[s.delay_count++] = QuantizeDelay{pitch, clamp_delay(delay)};
}

// Source held_notes.remove_value_first followed by note_offs_out.append_value: the release is
// emitted with the channel of the held entry it ends.
void release_held(QuantizeModuleState& s, std::uint8_t pitch, NoteBatch& output) noexcept {
  for (std::uint16_t i = 0; i < s.held.count; ++i) {
    if (s.held.items[i].pitch != pitch) continue;
    (void)output.off.append(Note{pitch, 0, s.held.items[i].channel});
    remove_index(s.held, i);
    return;
  }
}

// Advances the 64-bit transport tick by the signed 32-bit difference, so a backwards restart is
// followed exactly as the source's unbounded midi_tick is.
std::int64_t unwrapped_tick(QuantizeModuleState& s, std::uint32_t tick) noexcept {
  if (!s.tick_known) {
    s.tick = static_cast<std::int64_t>(tick);
    s.tick_known = true;
  } else {
    s.tick += static_cast<std::int32_t>(tick - s.last_tick);
  }
  s.last_tick = tick;
  return s.tick;
}

}  // namespace

void construct_quantize_state(ModuleState& state) noexcept { new (&state.storage.quantize) QuantizeModuleState{}; }

// Source Quantize.process.
void process_quantize(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.quantize;
  output = {};
  const std::int64_t current = unwrapped_tick(s, context.transport.tick);
  const auto rate = static_cast<std::int64_t>(music::kRateMidiTicks[s.rate_index]);
  const std::int64_t target = (floor_div(current, rate) + 1) * rate;
  const std::int64_t note_delay = target - current;

  // Note-ons: a held note is released first; a pending note is refreshed or queued for the grid.
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note note = input.on.items[i];
    release_held(s, note.pitch, output);
    for (std::uint8_t d = 0; d < s.scheduled_count;) {
      if (s.scheduled[d].note.pitch == note.pitch) {
        remove_scheduled(s, d);
      } else {
        ++d;
      }
    }

    const int pending = find_pending(s.pending, s.pending_count, note.pitch);
    if (pending >= 0) {
      s.pending[pending].note.velocity = note.velocity;
      s.pending[pending].due = target;
      record_delay(s, note.pitch, note_delay);
      continue;
    }
    if (s.pending_count >= kCapacityByte) {
      // Source drops the oldest pending note-on (it never sounds, so nothing can stick).
      const std::uint8_t old_pitch = s.pending[0].note.pitch;
      remove_pending(s, 0);
      const int old_delay = find_delay(s, old_pitch);
      if (old_delay >= 0) remove_delay(s, static_cast<std::uint8_t>(old_delay));
      ++s.pending_evicted;
    }
    s.pending[s.pending_count++] = QuantizeEntry{note, target};
    record_delay(s, note.pitch, note_delay);
  }

  // Note-offs keep the delay of their pending note-on, so they align to the same grid.
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const Note off = input.off.items[i];
    std::int64_t delay = note_delay;
    const int recorded = find_delay(s, off.pitch);
    if (recorded >= 0) {
      delay = s.delays[recorded].delay;
      remove_delay(s, static_cast<std::uint8_t>(recorded));
    }
    std::int64_t off_tick = current + delay + 1;
    // Deviation Q3: a release is never due before its own pending note-on is promoted. With a
    // monotonic tick this cannot happen; after a backwards restart the source releases first and
    // then promotes, which leaves the note sounding. Promotion runs before the release loop, so
    // a release due at the promotion tick still follows it.
    const int pending = find_pending(s.pending, s.pending_count, off.pitch);
    if (pending >= 0 && off_tick < s.pending[pending].due) off_tick = s.pending[pending].due;
    const int scheduled = find_pending(s.scheduled, s.scheduled_count, off.pitch);
    if (scheduled >= 0) {
      s.scheduled[scheduled].due = off_tick;
      continue;
    }
    if (s.scheduled_count >= kCapacityByte) {
      // Full schedule: the oldest release leaves now, as in the source.
      (void)output.off.append(Note{s.scheduled[0].note.pitch, 0, s.scheduled[0].note.channel});
      remove_scheduled(s, 0);
    }
    s.scheduled[s.scheduled_count++] = QuantizeEntry{Note{off.pitch, 0, off.channel}, off_tick};
  }

  // Promote pending note-ons whose grid tick has arrived, in pending order.
  std::uint8_t kept = 0;
  for (std::uint8_t i = 0; i < s.pending_count; ++i) {
    const QuantizeEntry entry = s.pending[i];
    if (entry.due > current) {
      s.pending[kept++] = entry;
      continue;
    }
    if (has_pitch(s.held, entry.note.pitch)) {
      release_held(s, entry.note.pitch, output);
    } else if (s.held.count >= kPolyphony) {
      (void)output.off.append(Note{s.held.items[0].pitch, 0, s.held.items[0].channel});
      remove_index(s.held, 0);
    }
    if (s.held.append(entry.note) != PushResult::Added) ++s.held_overflow;
    (void)output.on.append(entry.note);
  }
  s.pending_count = kept;

  // Release held notes whose quantized off tick has arrived. A release waits while the same
  // pitch has a note-on in this batch, as in the source.
  kept = 0;
  for (std::uint8_t i = 0; i < s.scheduled_count; ++i) {
    const QuantizeEntry entry = s.scheduled[i];
    if (entry.due > current) {
      s.scheduled[kept++] = entry;
      continue;
    }
    if (has_pitch(output.on, entry.note.pitch)) {
      s.scheduled[kept++] = entry;
      continue;
    }
    release_held(s, entry.note.pitch, output);
  }
  s.scheduled_count = kept;
}

// Source Quantize.stop clears pending, scheduled and held state without releasing it; the
// controller releases sounding notes on Stop/SceneChange/Panic. The tick tracking is not reset,
// because the source's midi_tick belongs to the transport.
void reset_quantize_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::ParameterChange) return;
  auto& s = state.storage.quantize;
  s.pending_count = 0;
  s.scheduled_count = 0;
  s.delay_count = 0;
  s.held.clear();
}

bool set_quantize_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&,
                        std::uint8_t& effects) noexcept {
  effects = EffectNone;
  std::int32_t v = 0;
  if (index != 0 || !int_in(value, 0, kQuantizeIndexMax, v)) return false;
  state.storage.quantize.rate_index = static_cast<std::uint8_t>(v);
  return true;
}

const ModuleDescriptor& quantize_module_descriptor() noexcept { return kQuantizeDescriptor; }

// ---------------------------------------------------------------- strum

namespace {

// Source get_tilt_velocity with STRUM_CHORD_LENGTH notes: double arithmetic, int() truncation.
std::uint8_t tilt_velocity(std::uint8_t velocity, std::uint8_t position, float tilt_value) noexcept {
  const auto tilt = static_cast<double>(tilt_value);
  const double pos_norm = static_cast<double>(position) / static_cast<double>(kStrumChordLength - 1);
  const double multiplier = tilt < 0.0 ? (1.0 + tilt) + (-tilt * pos_norm) : 1.0 - (tilt * pos_norm);
  const double scaled = static_cast<double>(velocity) * multiplier;
  // multiplier is within [0, 1], so the truncation is nonnegative.
  auto value = static_cast<std::int32_t>(scaled);
  if (value < 0) value = 0;
  if (value > 127) value = 127;
  return static_cast<std::uint8_t>(value);
}

std::uint64_t seed_of(std::int64_t count) noexcept { return static_cast<std::uint64_t>(count); }

}  // namespace

void construct_strum_state(ModuleState& state) noexcept { new (&state.storage.strum) StrumModuleState{}; }

// Source Strum.process. One note is emitted per pass at most, as in the source.
void process_strum(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.strum;
  output = {};

  // Add newly held chord tones to the register (source: contains by pitch).
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note note = input.on.items[i];
    if (has_pitch(s.register_notes, note.pitch)) continue;
    if (append_note(s.register_notes, note) != PushResult::Added) ++s.register_overflow;
  }

  // Released notes leave the register and are forwarded. Deviation S1: a pitch that is also
  // in this batch's on list is removed too, so no release is followed by a stuck note-on.
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    remove_first_pitch(s.register_notes, input.off.items[i].pitch);
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    (void)output.off.append(Note{input.off.items[i].pitch, 0, input.off.items[i].channel});
  }

  if (s.order == kOrderUp || s.order == kOrderDown || s.order == kOrderUpDown) {
    sort_ascending(s.register_notes);
    if (s.order == kOrderDown || (s.order == kOrderUpDown && s.strum_count % 2 == 0)) {
      reverse_notes(s.register_notes);
    }
  }

  if (!s.strum_active && s.register_notes.count > 0) {
    s.strum_active = true;
    s.next_strum_ms = context.now.ms;
  }
  if (s.strum_active && s.register_notes.count < 1) {
    s.strum_active = false;
    s.strum_position = 0;
    s.strum_count += 1;
  }

  // ticks_diff(next, now) < 0: the scheduled strum time is strictly in the past.
  if (s.strum_active && s.register_notes.count > 0 &&
      static_cast<std::int32_t>(s.next_strum_ms - context.now.ms) < 0) {
    std::uint16_t n = 0;
    if (s.order == kOrderRandom) {
      n = static_cast<std::uint16_t>(seeded::random_int(seed_of(s.strum_count),
                                                        static_cast<std::int64_t>(s.register_notes.count) - 1, 0));
    }
    const Note note = s.register_notes.items[n];
    const std::uint8_t velocity = tilt_velocity(note.velocity, s.strum_position, s.tilt);
    (void)output.on.append(Note{note.pitch, velocity, note.channel});
    remove_index(s.register_notes, n);

    // Articulation changes the delay before the next strummed note (source modifier table).
    std::int64_t modifier = 1;
    if (s.articulate == 1) {
      modifier = seeded::random_int(seed_of(s.strum_count + 25), 4, 1) + 10;
    } else if (s.articulate == 2) {
      modifier = seeded::random_int(seed_of(s.strum_count + 25), 9, 1) + 10;
    } else {
      // Source: STRUM_ARTICULATE_PATTERNS[articulate - 3] with Python negative indexing, so
      // ARTC=0 (EVEN) reads index 3.
      const auto index = static_cast<std::uint8_t>((s.articulate + 3) % kPatternCount);
      const auto position = s.strum_position < kPatternSteps - 1 ? s.strum_position : kPatternSteps - 1;
      modifier = kArticulationPatterns[index][position];
    }
    const auto delta = static_cast<std::int64_t>(static_cast<double>(s.amount) * (static_cast<double>(modifier) / 10.0));
    s.next_strum_ms = s.next_strum_ms + static_cast<std::uint32_t>(delta);
    s.strum_position = static_cast<std::uint8_t>((s.strum_position + 1) % kStrumChordLengthByte);
  }
}

// Source Strum.stop clears the register and outputs only. strum_active, the schedule and
// strum_count persist (the next pass with an empty register ends the strum, as in the source).
// Init and SceneChange build a fresh instance: all runtime state is cleared.
void reset_strum_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::ParameterChange) return;
  auto& s = state.storage.strum;
  s.register_notes.clear();
  if (reason == ResetReason::Init || reason == ResetReason::SceneChange) {
    s.strum_active = false;
    s.next_strum_ms = 0;
    s.strum_position = 0;
    s.strum_count = 0;
  }
}

bool set_strum_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&,
                     std::uint8_t& effects) noexcept {
  effects = EffectNone;
  auto& s = state.storage.strum;
  std::int32_t v = 0;
  switch (index) {
    case 0:
      if (!int_in(value, 0, kAmountMax, v)) return false;
      s.amount = v;
      return true;
    case 1:
      if (!int_in(value, 0, kOrderMax, v)) return false;
      s.order = static_cast<std::uint8_t>(v);
      return true;
    case 2:
      if (value.type != ParamType::Decimal || !(value.value.decimal >= -1.0f && value.value.decimal <= 1.0f)) {
        return false;
      }
      s.tilt = value.value.decimal;
      return true;
    case 3:
      if (!int_in(value, 1, kChordCountMax, v)) return false;
      s.tilt_note_count = v;
      return true;
    case 4:
      if (!int_in(value, 0, kArticulateMax, v)) return false;
      s.articulate = static_cast<std::uint8_t>(v);
      return true;
    default:
      return false;
  }
}

const ModuleDescriptor& strum_module_descriptor() noexcept { return kStrumDescriptor; }

}  // namespace orbit
