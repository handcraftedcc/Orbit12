#include "latch_pick.hpp"
#include <cstdint>
#include <limits>
#include <new>

// Deviations from circuitpython-source/src/apps/Orion/modules/latch.py and pick.py (each is also
// listed in the t09h report):
//  1. Latch: a note-on that is released or evicted in the same batch as its own pending note-on
//     cancels that pending on and sends no off. The source queues the off, then the on, so the
//     note sounds although latch state has released it (the output sends offs first). Held state
//     matches the source exactly; only the emitted note-ons/offs of that batch differ.
//  2. Latch: a release or note-on that cannot be queued is refused and counted (output_overflow),
//     leaving latch state unchanged. The source's note arrays are 12 entries and cannot overflow
//     with 12-entry input batches, so this is a defensive guard.
//  3. Pick: a selected note whose OCT-shifted pitch is outside MIDI 0..127 is not selected and is
//     counted (range_skipped). The source emits 128..255 (not valid MIDI) and raises ValueError
//     for negative values, which would stop the chain.
//  4. Pick: a note-on that finds the 12-entry held list full is dropped and counted (held_overflow).
//     The source drops it silently too; the note is never selected or sent, so nothing sticks.
// Exact, not deviations:
//  - Latch and pick match pitches only, as the source's remove_value_first/contains without a
//    channel. Releases carry the channel of the note they end (the source has no channels; the
//    output bitmap is keyed by channel and pitch, so this is the only release-safe choice).
//  - Latch ignores incoming note-offs (incoming offs never unlatch).
//  - Pick emits selection differences positionally, as emit_selection_change does. Releases for
//    previously sent notes always precede the on in the same batch.
//  - Stop/SceneChange/Panic clear latched or selected notes silently. The source does the same;
//    sounding notes are released by the controller via Output::all_notes_off, as for arp.

namespace orbit {
namespace {
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr std::int64_t kMidiNoteMax = 127;
constexpr std::int64_t kOctaveSemitones = 12;
constexpr auto kCapacity = static_cast<std::uint16_t>(kPolyphony);

constexpr std::uint8_t kPickEnd = 1;
constexpr std::uint8_t kPickLow = 2;
constexpr std::uint8_t kPickHigh = 3;

constexpr const char* kPolyLabel = "POLY";
constexpr const char* kPickModeLabel = "MDE";
constexpr const char* kCountLabel = "COUNT";
constexpr const char* kInvertLabel = "INV";
constexpr const char* kOffsetLabel = "OFST";
constexpr const char* kOctavesLabel = "OCT";
constexpr const char* kPickModeOptions[] = {"STRT", "END", "LOW", "HIGH"};

const ParamSpec kLatchParams[] = {
    {"polyphony", ParamType::Integer, ParamValue::integer(12), ParamValue::integer(1), ParamValue::integer(12), ParamValue::integer(1), kPolyLabel, nullptr, 0, ParamPersist}};

const ParamSpec kPickParams[] = {
    {"pick_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(3), ParamValue::integer(1), kPickModeLabel, kPickModeOptions, 4, ParamPersist},
    {"count", ParamType::Integer, ParamValue::integer(1), ParamValue::integer(1), ParamValue::integer(12), ParamValue::integer(1), kCountLabel, nullptr, 0, ParamPersist},
    {"invert", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), kInvertLabel, nullptr, 0, ParamPersist},
    {"offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(12), ParamValue::integer(1), kOffsetLabel, nullptr, 0, ParamPersist},
    {"octaves", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOctavesLabel, nullptr, 0, ParamPersist}};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kLatchDescriptor{"latch", 1, kLatchParams, 1, construct_latch_state, descriptor_destroy, process_latch, reset_latch_state, set_latch_param, attr_none};
const ModuleDescriptor kPickDescriptor{"pick", 1, kPickParams, 5, construct_pick_state, descriptor_destroy, process_pick, reset_pick_state, set_pick_param, attr_none};

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

// Index of the oldest entry with this pitch, or -1.
std::int32_t find_pitch(const NoteBuffer& buffer, std::uint8_t pitch) noexcept {
  for (std::uint16_t i = 0; i < buffer.count; ++i) {
    if (buffer.items[i].pitch == pitch) return static_cast<std::int32_t>(i);
  }
  return -1;
}

// Removes one entry, keeping the order of the rest (source remove_index).
void remove_at(NoteBuffer& buffer, std::uint16_t index) noexcept {
  for (std::uint16_t i = static_cast<std::uint16_t>(index + 1); i < buffer.count; ++i) buffer.items[i - 1] = buffer.items[i];
  --buffer.count;
}

bool is_full(const NoteBuffer& buffer) noexcept { return buffer.count >= kCapacity; }

}  // namespace

// ---------------------------------------------------------------- Latch

void construct_latch_state(ModuleState& state) noexcept { new (&state.storage.latch) LatchModuleState{}; }

// Releases latched entry `index`. A note-on for this pitch still pending in this batch is cancelled
// (deviation 1); otherwise an off is queued on the note's own channel. Returns false, changing
// nothing, when the off cannot be queued (deviation 2).
bool release_latched(LatchModuleState& s, NoteBatch& output, std::uint16_t index) noexcept {
  const Note latched = s.held.items[index];
  const auto pending = find_pitch(output.on, latched.pitch);
  if (pending >= 0) {
    remove_at(output.on, static_cast<std::uint16_t>(pending));
  } else {
    if (output.off.count >= kCapacity) return false;
    output.off.append(Note{latched.pitch, 0, latched.channel});
  }
  remove_at(s.held, index);
  return true;
}

// Source Latch.process. Incoming note-offs are not used. A note-on for a latched pitch toggles it
// off; a new note-on at capacity first evicts the oldest latched note (one per note-on, as source).
void process_latch(ModuleState& state, const ProcessContext&, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.latch;
  output = {};
  std::int32_t limit = s.polyphony;
  if (limit < 1) limit = 1;
  if (limit > static_cast<std::int32_t>(kCapacity)) limit = static_cast<std::int32_t>(kCapacity);

  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note note = input.on.items[i];
    const auto latched = find_pitch(s.held, note.pitch);
    if (latched >= 0) {
      if (!release_latched(s, output, static_cast<std::uint16_t>(latched))) ++s.output_overflow;
      continue;
    }
    if (output.on.count >= kCapacity) {
      ++s.output_overflow;
      continue;
    }
    if (s.held.count >= limit && !release_latched(s, output, 0)) {
      ++s.output_overflow;
      continue;
    }
    if (is_full(s.held)) {
      ++s.held_overflow;
      continue;
    }
    s.held.append(note);
    output.on.append(note);
  }
}

void reset_latch_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  state.storage.latch.held.clear();
}

bool set_latch_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = 0;
  std::int32_t v = 0;
  if (index != 0 || !int_in(value, 1, 12, v)) return false;
  state.storage.latch.polyphony = v;
  return true;
}

const ModuleDescriptor& latch_module_descriptor() noexcept { return kLatchDescriptor; }

// ---------------------------------------------------------------- Pick

void construct_pick_state(ModuleState& state) noexcept { new (&state.storage.pick) PickModuleState{}; }

namespace {
// Source ordered_indexes. START keeps held order; END reverses it; LOW and HIGH sort by pitch with a
// stable sort (ties keep held order) and HIGH reverses that result, so tied notes come last-first.
void ordered_indexes(const PickModuleState& s, std::uint8_t (&order)[kPolyphony]) noexcept {
  const auto n = static_cast<std::uint8_t>(s.held.count);
  for (std::uint8_t i = 0; i < n; ++i) order[i] = i;
  if (s.pick_mode == kPickEnd) {
    for (std::uint8_t lo = 0, hi = static_cast<std::uint8_t>(n == 0 ? 0 : n - 1); lo < hi; ++lo, --hi) {
      const auto tmp = order[lo];
      order[lo] = order[hi];
      order[hi] = tmp;
    }
  } else if (s.pick_mode == kPickLow || s.pick_mode == kPickHigh) {
    for (std::uint8_t i = 1; i < n; ++i) {
      const auto key = order[i];
      const auto pitch = s.held.items[key].pitch;
      std::uint8_t j = i;
      while (j > 0 && s.held.items[order[j - 1]].pitch > pitch) {
        order[j] = order[j - 1];
        --j;
      }
      order[j] = key;
    }
    if (s.pick_mode == kPickHigh) {
      for (std::uint8_t lo = 0, hi = static_cast<std::uint8_t>(n == 0 ? 0 : n - 1); lo < hi; ++lo, --hi) {
        const auto tmp = order[lo];
        order[lo] = order[hi];
        order[hi] = tmp;
      }
    }
  }
}

// Source append_next_note: held pitch shifted by OCT octaves, velocity and channel of the held note.
void append_next(PickModuleState& s, std::uint8_t index, NoteBuffer& next) noexcept {
  const Note& held = s.held.items[index];
  const std::int64_t pitch = static_cast<std::int64_t>(held.pitch) + static_cast<std::int64_t>(s.octaves) * kOctaveSemitones;
  if (pitch < 0 || pitch > kMidiNoteMax) {
    ++s.range_skipped;  // Deviation 3.
    return;
  }
  if (next.append(Note{static_cast<std::uint8_t>(pitch), held.velocity, held.channel}) != PushResult::Added) {
    ++s.output_overflow;  // Unreachable: next holds at most the 12 held notes.
  }
}

// Source build_next_notes: selects COUNT entries after OFST in pick order, or the complement when INV.
void build_next_notes(PickModuleState& s, NoteBuffer& next) noexcept {
  next.clear();
  const auto n = static_cast<std::int64_t>(s.held.count);
  if (n == 0) return;
  std::uint8_t order[kPolyphony]{};
  ordered_indexes(s, order);

  std::int64_t count = s.count < 0 ? 0 : s.count;
  if (count > n) count = n;
  std::int64_t offset = s.offset < 0 ? 0 : s.offset;
  // Python slice order[offset:offset+count]; out-of-range slices are empty or clipped.
  const std::int64_t begin = offset < n ? offset : n;
  const std::int64_t end = (offset + count) < n ? (offset + count) : n;

  if (!s.invert) {
    for (std::int64_t k = begin; k < end; ++k) append_next(s, order[k], next);
    return;
  }
  for (std::int64_t k = 0; k < n; ++k) {
    if (k < begin || k >= end) append_next(s, order[k], next);
  }
}

// Source selection_changed: different length, or any pitch/velocity differs in order.
bool selection_changed(const NoteBuffer& active, const NoteBuffer& next) noexcept {
  if (active.count != next.count) return true;
  for (std::uint16_t i = 0; i < active.count; ++i) {
    if (active.items[i].pitch != next.items[i].pitch || active.items[i].velocity != next.items[i].velocity) return true;
  }
  return false;
}

// Source emit_selection_change: compares positions. Unchanged positions emit nothing; otherwise the
// previously sent note is released and the new one sent. The output starts empty each pass, and both
// lists hold at most kPolyphony entries, so the appends below cannot be refused.
void emit_selection_change(PickModuleState& s, const NoteBuffer& next, NoteBatch& output) noexcept {
  const auto length = s.active.count > next.count ? s.active.count : next.count;
  for (std::uint16_t i = 0; i < length; ++i) {
    const bool old_active = i < s.active.count;
    const bool new_active = i < next.count;
    if (old_active && new_active && s.active.items[i].pitch == next.items[i].pitch &&
        s.active.items[i].velocity == next.items[i].velocity) {
      continue;
    }
    if (old_active && output.off.append(Note{s.active.items[i].pitch, 0, s.active.items[i].channel}) != PushResult::Added) {
      ++s.output_overflow;  // Unreachable, see above.
    }
    if (new_active && output.on.append(next.items[i]) != PushResult::Added) {
      ++s.output_overflow;  // Unreachable, see above.
    }
  }
  s.active = next;
}
}  // namespace

// Source Pick.process: held notes are updated first (ons, then offs), then the selection is rebuilt
// and only a change emits releases and note-ons.
void process_pick(ModuleState& state, const ProcessContext&, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.pick;
  output = {};

  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    if (is_full(s.held)) {
      ++s.held_overflow;  // Source drops the note silently; it is never selected or sent.
      continue;
    }
    s.held.append(input.on.items[i]);
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const auto at = find_pitch(s.held, input.off.items[i].pitch);
    if (at >= 0) remove_at(s.held, static_cast<std::uint16_t>(at));
  }

  NoteBuffer next{};
  build_next_notes(s, next);
  if (selection_changed(s.active, next)) emit_selection_change(s, next, output);
}

// Source Pick.stop: held and selected notes are cleared silently; the controller releases sounding notes.
void reset_pick_state(ModuleState& state, ResetReason reason) noexcept {
  if (!stop_like(reason)) return;
  auto& s = state.storage.pick;
  s.held.clear();
  s.active.clear();
}

bool set_pick_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = 0;
  auto& s = state.storage.pick;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, 0, 3, v)) return false; s.pick_mode = static_cast<std::uint8_t>(v); return true;
    case 1: if (!int_in(value, 1, 12, v)) return false; s.count = v; return true;
    case 2:
      if (value.type != ParamType::Boolean) return false;
      s.invert = value.value.boolean;
      return true;
    case 3: if (!int_in(value, 0, 12, v)) return false; s.offset = v; return true;
    case 4: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.octaves = v; return true;
    default: return false;
  }
}

const ModuleDescriptor& pick_module_descriptor() noexcept { return kPickDescriptor; }
}  // namespace orbit
