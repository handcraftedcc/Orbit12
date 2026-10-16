#include "walk_inputs.hpp"
#include "../music.hpp"
#include <cstdint>
#include <limits>
#include <new>

namespace orbit {
namespace {
using i64 = std::int64_t;

constexpr const char* kSusOptions[] = {"OFF", "SUS2", "SUS4"};
constexpr i64 kIntMin = std::numeric_limits<std::int32_t>::min();
constexpr i64 kIntMax = std::numeric_limits<std::int32_t>::max();

// KEYCOLORBASE=colorwheel(128), KEYCOLORROOT=colorwheel(64), KEYCOLORNAVPARMS=colorwheel(21).
constexpr RgbColor kBase{0, 126, 129};
constexpr RgbColor kRoot{63, 192, 0};
constexpr RgbColor kChord{192, 63, 0};

// Source chord_walk.py constants (logical pads).
constexpr std::uint8_t kChordPads[4] = {0, 3, 6, 9};
constexpr std::uint8_t kTriggerPads[8] = {1, 2, 4, 5, 7, 8, 10, 11};
constexpr std::int32_t kChordOffsetRequest = -2;

const ParamSpec kNoteWalkParams[] = {
  {"octave", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(static_cast<std::int32_t>(kIntMin)), ParamValue::integer(static_cast<std::int32_t>(kIntMax)), ParamValue::integer(1), "OCT", nullptr, 0, ParamPersist, GlobalBinding::Octave},
  {"key_offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(static_cast<std::int32_t>(kIntMin)), ParamValue::integer(static_cast<std::int32_t>(kIntMax)), ParamValue::integer(1), "PADOFS", nullptr, 0, ParamPersist, GlobalBinding::KeyOffset},
  {"velocity", ParamType::Integer, ParamValue::integer(127), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist}};

const ParamSpec kChordWalkParams[] = {
  {"pad", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(3), ParamValue::integer(1), "PAD", nullptr, 0, ParamPersist},
  {"note", ParamType::Integer, ParamValue::integer(1), ParamValue::integer(-24), ParamValue::integer(24), ParamValue::integer(1), "NOTE", nullptr, 0, ParamPersist},
  {"seventh", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "7TH", nullptr, 0, ParamPersist},
  {"sus", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), "SUS", kSusOptions, 3, ParamPersist},
  {"power", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "PWR", nullptr, 0, ParamPersist},
  {"preview_chord", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "PRVCRD", nullptr, 0, ParamPersist},
  {"octave", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(static_cast<std::int32_t>(kIntMin)), ParamValue::integer(static_cast<std::int32_t>(kIntMax)), ParamValue::integer(1), "OCT", nullptr, 0, ParamPersist, GlobalBinding::Octave},
  {"key_offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(static_cast<std::int32_t>(kIntMin)), ParamValue::integer(static_cast<std::int32_t>(kIntMax)), ParamValue::integer(1), "PADOFS", nullptr, 0, ParamPersist, GlobalBinding::KeyOffset},
  {"velocity", ParamType::Integer, ParamValue::integer(127), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist}};

i64 floor_div(i64 value, i64 divisor) noexcept {
  auto q = value / divisor;
  if (value % divisor < 0) --q;
  return q;
}
i64 py_mod(i64 value, i64 divisor) noexcept { return value - floor_div(value, divisor) * divisor; }

std::uint8_t clamp_midi(i64 v) noexcept {
  if (v < 0) return 0;
  if (v > kMidiMax) return kMidiMax;
  return static_cast<std::uint8_t>(v);
}

// Source PADMAP.index: physical key id to logical pad (0..11).
std::uint8_t pad_of_key(std::uint8_t key) noexcept {
  for (std::uint8_t i = 0; i < kKeys; ++i) {
    if (music::kPadMap[i] == key) return i;
  }
  return 0;  // Unreachable for valid keys; callers filter with KeyEvent::valid().
}

std::size_t scale_index(const GlobalState& g) noexcept {
  return g.scale < music::kScaleLengths.size() ? static_cast<std::size_t>(g.scale) : std::size_t{1};
}

// Source Music.transpose(note, degrees, 0, True, key, scale) in unbounded
// integers. The result is clamped to MIDI range, as in Python.
std::uint8_t transpose_by_degrees(i64 note, i64 degrees, const GlobalState& g) noexcept {
  const auto si = scale_index(g);
  const i64 length = music::kScaleLengths[si];
  const i64 root = g.key;
  const i64 normalized = py_mod(note - root, 12);
  const i64 octave = floor_div(note - root, 12);
  std::size_t closest = 0;
  i64 distance = static_cast<i64>(music::kScales[si][0]) - normalized;
  if (distance < 0) distance = -distance;
  for (std::size_t i = 1; i < static_cast<std::size_t>(length); ++i) {
    i64 candidate = static_cast<i64>(music::kScales[si][i]) - normalized;
    if (candidate < 0) candidate = -candidate;
    if (candidate < distance) {
      distance = candidate;
      closest = i;
    }
  }
  const i64 new_index = static_cast<i64>(closest) + degrees;
  const i64 shift = floor_div(new_index, length);
  const i64 index = new_index - shift * length;
  return clamp_midi(static_cast<i64>(music::kScales[si][static_cast<std::size_t>(index)]) +
                    (octave + shift) * 12 + root);
}

// Source Input.convert_note, unclamped in unbounded integers.
i64 convert_key_to_note(std::uint8_t key, const GlobalState& g) noexcept {
  const auto si = scale_index(g);
  const i64 length = music::kScaleLengths[si];
  const i64 pad = static_cast<i64>(pad_of_key(key)) + g.key_offset;
  const i64 degree = py_mod(pad, length);
  const i64 octave = floor_div(pad, length) + g.octave;
  return static_cast<i64>(music::kScales[si][static_cast<std::size_t>(degree)]) + g.key + (octave + 2) * 12;
}

// Source chord_walk.note_for_degree.
std::uint8_t chord_note_for_degree(i64 degree, const GlobalState& g) noexcept {
  const auto si = scale_index(g);
  const i64 length = music::kScaleLengths[si];
  const i64 octave = floor_div(degree, length) + g.octave;
  const auto index = static_cast<std::size_t>(py_mod(degree, length));
  return clamp_midi(static_cast<i64>(music::kScales[si][index]) + g.key + (octave + 2) * 12);
}

bool take_int(const ParamValue& v, i64 lo, i64 hi, i64& out) noexcept {
  if (v.type != ParamType::Integer || v.value.integer < lo || v.value.integer > hi) return false;
  out = v.value.integer;
  return true;
}
bool take_enum(const ParamValue& v, std::int32_t hi, std::int32_t& out) noexcept {
  if (v.type != ParamType::Enumeration || v.value.integer < 0 || v.value.integer > hi) return false;
  out = v.value.integer;
  return true;
}
bool take_bool(const ParamValue& v, bool& out) noexcept {
  if (v.type != ParamType::Boolean) return false;
  out = v.value.boolean;
  return true;
}

// Source set_active_note. Capacity is checked before any state changes, so on
// overflow the previous note keeps sounding and stays tracked (no lost off).
void set_active(std::int16_t& active, std::uint32_t& overflow, std::uint8_t note, std::uint8_t velocity,
                NoteBatch& out) noexcept {
  if (active == note) return;
  const bool need_off = active >= 0;
  if ((need_off && out.off.count >= out.off.capacity()) || out.on.count >= out.on.capacity()) {
    ++overflow;
    return;
  }
  if (need_off) (void)out.off.append(Note{static_cast<std::uint8_t>(active), 0, 0});
  (void)out.on.append(Note{note, velocity, 0});
  active = note;
}

// Emits the retained active note-off. Returns false (and keeps it tracked for
// the next call) when the off buffer is full.
bool emit_active_off(std::int16_t& active, std::uint32_t& overflow, NoteBatch& out) noexcept {
  if (out.off.count >= out.off.capacity()) {
    ++overflow;
    return false;
  }
  (void)out.off.append(Note{static_cast<std::uint8_t>(active), 0, 0});
  active = -1;
  return true;
}

// Source Input.color_pixels (base roots only; NoteWalk does not override it).
void note_walk_colors(const ProcessContext& c, InputView& view) noexcept {
  for (auto& color : view.pad_colors) color = kBase;
  const i64 length = music::kScaleLengths[scale_index(c.global)];
  for (std::uint8_t idx = 0; idx < kKeys; ++idx) {
    if (py_mod(static_cast<i64>(idx) + c.global.key_offset, length) == 0) {
      view.pad_colors[pad_of_key(idx)] = kRoot;
    }
  }
}

// ---- note_walk ----

void note_remove_held(NoteWalkInputState& s, std::uint8_t pad) noexcept {
  for (std::uint8_t i = 0; i < s.held_count; ++i) {
    if (s.held_pads[i] != pad) continue;
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < s.held_count; ++j) {
      s.held_pads[j - 1] = s.held_pads[j];
      s.held_velocities[j - 1] = s.held_velocities[j];
    }
    --s.held_count;
    break;
  }
  if (s.offset_root == pad && s.held_count > 0) s.offset_root = s.held_pads[s.held_count - 1];
}

// Source add_held_pad: remove an existing entry first, then append.
void note_add_held(NoteWalkInputState& s, std::uint8_t pad, std::uint8_t velocity) noexcept {
  for (std::uint8_t i = 0; i < s.held_count; ++i) {
    if (s.held_pads[i] != pad) continue;
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < s.held_count; ++j) {
      s.held_pads[j - 1] = s.held_pads[j];
      s.held_velocities[j - 1] = s.held_velocities[j];
    }
    --s.held_count;
    break;
  }
  if (s.held_count < kKeys) {
    s.held_pads[s.held_count] = pad;
    s.held_velocities[s.held_count] = velocity;
    ++s.held_count;
  }
}

void note_reset_walk(NoteWalkInputState& s) noexcept {
  s.held_count = 0;
  s.has_note_root = false;
  s.note_root = 0;
  s.offset_root = -1;
  s.current_steps = 0;
  s.active_note = -1;
}

// ---- chord_walk ----

void chord_build_register(ChordWalkInputState& s, std::uint8_t slot, const GlobalState& g) noexcept {
  const i64 root = static_cast<i64>(s.notes[slot]) - 1;
  i64 degrees[4]{};
  std::uint8_t count = 0;
  degrees[count++] = root;
  if (s.sus[slot] == 1) {
    degrees[count++] = root + 1;
  } else if (s.sus[slot] == 2) {
    degrees[count++] = root + 3;
  } else if (!s.power[slot]) {
    degrees[count++] = root + 2;
  }
  degrees[count++] = root + 4;
  if (s.sevenths[slot]) degrees[count++] = root + 6;

  s.register_count = 0;
  for (std::uint8_t i = 0; i < count; ++i) {
    s.register_notes[s.register_count++] = chord_note_for_degree(degrees[i], g);
  }
  // Insertion sort ascending (source NoteArray.sort_notes).
  for (std::uint8_t i = 1; i < s.register_count; ++i) {
    const auto key = s.register_notes[i];
    std::uint8_t j = i;
    while (j > 0 && s.register_notes[j - 1] > key) {
      s.register_notes[j] = s.register_notes[j - 1];
      --j;
    }
    s.register_notes[j] = key;
  }
}

// Register value at a step: register[step mod n] + 12 * floor(step / n).
i64 walk_value(const ChordWalkInputState& s, i64 step) noexcept {
  const i64 n = s.register_count;
  return static_cast<i64>(s.register_notes[py_mod(step, n)]) + 12 * floor_div(step, n);
}

// Largest step <= start whose value is <= 127. Equivalent to the source
// descending loop, computed per octave so the work is bounded.
i64 largest_step_at_or_below(const ChordWalkInputState& s, i64 start) noexcept {
  const i64 n = s.register_count;
  const i64 q0 = floor_div(start, n);
  const i64 r0 = py_mod(start, n);
  const i64 qmax = floor_div(127 - static_cast<i64>(s.register_notes[0]), 12);
  const bool above_limit = q0 > qmax;
  const i64 q = above_limit ? qmax : q0;
  i64 last = 0;
  for (i64 k = 0; k < n; ++k) {
    if (static_cast<i64>(s.register_notes[k]) + 12 * q <= 127) last = k;
  }
  return q * n + (above_limit ? last : (r0 < last ? r0 : last));
}

// Smallest step >= start whose value is >= 0. Equivalent to the source ascending loop.
i64 smallest_step_at_or_above(const ChordWalkInputState& s, i64 start) noexcept {
  const i64 n = s.register_count;
  const i64 q0 = floor_div(start, n);
  const i64 r0 = py_mod(start, n);
  const i64 qmin = -floor_div(static_cast<i64>(s.register_notes[n - 1]), 12);
  const bool below_limit = q0 < qmin;
  const i64 q = below_limit ? qmin : q0;
  i64 first = n - 1;
  for (i64 k = n - 1; k >= 0; --k) {
    if (static_cast<i64>(s.register_notes[k]) + 12 * q >= 0) first = k;
  }
  return q * n + (below_limit ? first : (r0 > first ? r0 : first));
}

// Source ChordWalk.note_for_step; updates current_step as the source does.
// Returns -1 for Python None (empty register).
std::int16_t chord_note_for_step(ChordWalkInputState& s) noexcept {
  if (s.register_count == 0) return -1;
  const i64 value = walk_value(s, s.current_step);
  if (value > 127) {
    s.current_step = largest_step_at_or_below(s, s.current_step);
  } else if (value < 0) {
    s.current_step = smallest_step_at_or_above(s, s.current_step);
  }
  return static_cast<std::int16_t>(walk_value(s, s.current_step));
}

int chord_slot(std::uint8_t pad) noexcept {
  for (int i = 0; i < 4; ++i) {
    if (kChordPads[i] == pad) return i;
  }
  return -1;
}
int trigger_index(std::uint8_t pad) noexcept {
  for (int i = 0; i < 8; ++i) {
    if (kTriggerPads[i] == pad) return i;
  }
  return -1;
}

// Raw removal of the first matching trigger (source remove_value_first).
void chord_erase_trigger(ChordWalkInputState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = 0; i < s.held_trigger_count; ++i) {
    if (s.held_triggers[i] != index) continue;
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < s.held_trigger_count; ++j) {
      s.held_triggers[j - 1] = s.held_triggers[j];
      s.held_trigger_velocities[j - 1] = s.held_trigger_velocities[j];
    }
    --s.held_trigger_count;
    return;
  }
}

// Source remove_held_trigger: erase, then re-derive trigger_root when it was removed.
void chord_remove_trigger(ChordWalkInputState& s, std::uint8_t index) noexcept {
  chord_erase_trigger(s, index);
  if (s.trigger_root == index && s.held_trigger_count > 0) {
    s.trigger_root = s.held_triggers[s.held_trigger_count - 1];
  }
}

// Source add_held_trigger: erase an existing entry (raw), then append.
void chord_add_trigger(ChordWalkInputState& s, std::uint8_t index, std::uint8_t velocity) noexcept {
  chord_erase_trigger(s, index);
  if (s.held_trigger_count < ChordWalkInputState::kTriggerCount) {
    s.held_triggers[s.held_trigger_count] = index;
    s.held_trigger_velocities[s.held_trigger_count] = velocity;
    ++s.held_trigger_count;
  }
}

void chord_add_held_chord(ChordWalkInputState& s, std::uint8_t slot) noexcept {
  for (std::uint8_t i = 0; i < s.held_chord_count; ++i) {
    if (s.held_chords[i] != slot) continue;
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < s.held_chord_count; ++j) s.held_chords[j - 1] = s.held_chords[j];
    --s.held_chord_count;
    break;
  }
  if (s.held_chord_count < ChordWalkInputState::kPadCount) s.held_chords[s.held_chord_count++] = slot;
}

void chord_reset_triggers(ChordWalkInputState& s) noexcept {
  s.held_trigger_count = 0;
  s.trigger_root = -1;
  s.current_step = 0;
  s.active_note = -1;
}

// Preview note-offs are owed state: emitted now when the batch has room and
// nothing is queued, otherwise queued and drained by a later call.
void chord_emit_or_queue_off(ChordWalkInputState& s, std::uint8_t pitch, NoteBatch& out) noexcept {
  if (s.pending_off_count == 0 && out.off.count < out.off.capacity()) {
    (void)out.off.append(Note{pitch, 0, 0});
  } else if (s.pending_off_count < ChordWalkInputState::kPendingOffCapacity) {
    s.pending_off[s.pending_off_count++] = pitch;
  } else {
    ++s.output_overflow;
  }
}

void chord_drain_pending(ChordWalkInputState& s, NoteBatch& out) noexcept {
  std::uint16_t drained = 0;
  while (drained < s.pending_off_count && out.off.count < out.off.capacity()) {
    (void)out.off.append(Note{s.pending_off[drained], 0, 0});
    ++drained;
  }
  for (std::uint16_t i = drained; i < s.pending_off_count; ++i) {
    s.pending_off[i - drained] = s.pending_off[i];
  }
  s.pending_off_count = static_cast<std::uint16_t>(s.pending_off_count - drained);
}

// Source ChordWalk.color_pixels.
void chord_colors(const ChordWalkInputState& s, const GlobalState& g, InputView& view) noexcept {
  for (auto& color : view.pad_colors) color = kBase;
  for (const auto pad : kChordPads) view.pad_colors[pad_of_key(pad)] = kChord;
  if (s.register_count > 0) {
    for (i64 i = 0; i < 8; ++i) {
      if (py_mod(i + g.key_offset, s.register_count) == 0) view.pad_colors[pad_of_key(kTriggerPads[i])] = kRoot;
    }
  }
}

// Source set_pad_value: rebuild the register only when the edited pad is the last held chord.
void chord_rebuild_if_last_held(ChordWalkInputState& s, const GlobalState& g, std::uint8_t& effects) noexcept {
  effects = EffectNone;
  if (s.held_chord_count && s.held_chords[s.held_chord_count - 1] == s.pad) {
    chord_build_register(s, s.pad, g);
    effects = EffectRepaint;
  }
}

void destroy(InputState&) noexcept {}
}  // namespace

void construct_note_walk_input(InputState& state) noexcept { new (&state.storage.note_walk) NoteWalkInputState{}; }

void construct_chord_walk_input(InputState& state) noexcept {
  auto& s = *new (&state.storage.chord_walk) ChordWalkInputState{};
  // Source __init__: held_chords starts as [0]; the register is built on the
  // first process call because construct has no GlobalState view.
  s.held_chords[0] = 0;
  s.held_chord_count = 1;
  s.key_offset_request_pending = true;
  s.initial_register_pending = true;
}

void process_note_walk_input(InputState& state, const ProcessContext& c, const InputFrame& frame, NoteBatch& out,
                             InputView& view) noexcept {
  auto& s = state.storage.note_walk;
  out = NoteBatch{};
  note_walk_colors(c, view);

  std::uint8_t downs[kInputEventCapacity]{};
  std::uint8_t ups[kInputEventCapacity]{};
  std::uint16_t down_count = 0;
  std::uint16_t up_count = 0;
  for (std::uint16_t i = 0; i < frame.keys.count; ++i) {
    const auto& e = frame.keys.items[i];
    if (!e.valid()) continue;
    if (e.down) {
      downs[down_count++] = e.key;
    } else {
      ups[up_count++] = e.key;
    }
  }

  // Source processes note_offs before note_ons.
  for (std::uint16_t i = 0; i < up_count; ++i) note_remove_held(s, pad_of_key(ups[i]));
  // Source: once no pads are held the sounding note is released and the walk
  // resets. A full off buffer keeps the note tracked; the next call retries.
  if (s.held_count == 0 && s.active_note >= 0) {
    if (emit_active_off(s.active_note, s.output_overflow, out)) note_reset_walk(s);
  }

  for (std::uint16_t i = 0; i < down_count; ++i) {
    const auto pad = pad_of_key(downs[i]);
    if (s.held_count == 0) {
      s.note_root = convert_key_to_note(downs[i], c.global);
      s.has_note_root = true;
      s.offset_root = pad;
      s.current_steps = 0;
    } else {
      s.current_steps += static_cast<i64>(pad) - s.offset_root;
    }
    note_add_held(s, pad, s.velocity);
    set_active(s.active_note, s.output_overflow, transpose_by_degrees(s.note_root, s.current_steps, c.global),
               s.velocity, out);
  }
}

void process_chord_walk_input(InputState& state, const ProcessContext& c, const InputFrame& frame, NoteBatch& out,
                              InputView& view) noexcept {
  auto& s = state.storage.chord_walk;
  out = NoteBatch{};
  if (s.initial_register_pending) {
    chord_build_register(s, 0, c.global);
    s.initial_register_pending = false;
  }
  chord_colors(s, c.global, view);
  chord_drain_pending(s, out);

  std::uint8_t downs[kInputEventCapacity]{};
  std::uint8_t ups[kInputEventCapacity]{};
  std::uint16_t down_count = 0;
  std::uint16_t up_count = 0;
  for (std::uint16_t i = 0; i < frame.keys.count; ++i) {
    const auto& e = frame.keys.items[i];
    if (!e.valid()) continue;
    if (e.down) {
      downs[down_count++] = e.key;
    } else {
      ups[up_count++] = e.key;
    }
  }

  for (std::uint16_t i = 0; i < up_count; ++i) {
    const auto pad = pad_of_key(ups[i]);
    const int slot = chord_slot(pad);
    if (slot >= 0) {
      if (s.preview_chord) {
        chord_build_register(s, static_cast<std::uint8_t>(slot), c.global);
        for (std::uint8_t j = 0; j < s.register_count; ++j) chord_emit_or_queue_off(s, s.register_notes[j], out);
      }
      continue;
    }
    const int trigger = trigger_index(pad);
    if (trigger >= 0) chord_remove_trigger(s, static_cast<std::uint8_t>(trigger));
  }
  // Source: active note released once no trigger is held; retried if the off buffer is full.
  if (s.held_trigger_count == 0 && s.active_note >= 0) {
    if (emit_active_off(s.active_note, s.output_overflow, out)) chord_reset_triggers(s);
  }

  for (std::uint16_t i = 0; i < down_count; ++i) {
    const auto pad = pad_of_key(downs[i]);
    const int slot = chord_slot(pad);
    if (slot >= 0) {
      if (s.follow_played_pad) s.pad = static_cast<std::uint8_t>(slot);
      chord_add_held_chord(s, static_cast<std::uint8_t>(slot));
      chord_build_register(s, static_cast<std::uint8_t>(slot), c.global);
      if (s.preview_chord) {
        for (std::uint8_t j = 0; j < s.register_count; ++j) {
          if (out.on.count < out.on.capacity()) {
            (void)out.on.append(Note{s.register_notes[j], s.velocity, 0});
          } else {
            ++s.output_overflow;
          }
        }
      }
      continue;
    }
    const int trigger = trigger_index(pad);
    if (trigger < 0) continue;
    const auto index = static_cast<std::uint8_t>(trigger);
    if (s.held_trigger_count == 0) {
      s.current_step = static_cast<i64>(trigger) + c.global.key_offset;
      s.trigger_root = static_cast<std::int16_t>(trigger);
    } else {
      s.current_step += static_cast<i64>(trigger) - s.trigger_root;
    }
    chord_add_trigger(s, index, s.velocity);
    const auto note = chord_note_for_step(s);
    if (note >= 0) set_active(s.active_note, s.output_overflow, static_cast<std::uint8_t>(note), s.velocity, out);
  }
}

void reset_note_walk_input(InputState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic) {
    note_reset_walk(state.storage.note_walk);
  }
}

void reset_chord_walk_input(InputState& state, ResetReason reason) noexcept {
  if (reason != ResetReason::Stop && reason != ResetReason::SceneChange && reason != ResetReason::Panic) return;
  auto& s = state.storage.chord_walk;
  // Source stop(): held chords, register and triggers are cleared. Owed preview
  // note-offs are kept for later calls.
  s.held_chord_count = 0;
  s.register_count = 0;
  chord_reset_triggers(s);
}

bool set_note_walk_input_param(InputState& state, std::uint8_t index, ParamValue v, const GlobalState&,
                               std::uint8_t& effects) noexcept {
  effects = EffectNone;
  auto& s = state.storage.note_walk;
  i64 n = 0;
  switch (index) {
    case 0:
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = EffectReleaseNotes | EffectRepaint;
      return true;
    case 1:
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = EffectReleaseNotes | EffectRepaint;
      return true;
    case 2:
      if (!take_int(v, 0, 127, n)) return false;
      s.velocity = static_cast<std::uint8_t>(n);
      return true;
    default:
      return false;
  }
}

bool set_chord_walk_input_param(InputState& state, std::uint8_t index, ParamValue v, const GlobalState& g,
                                std::uint8_t& effects) noexcept {
  effects = EffectNone;
  auto& s = state.storage.chord_walk;
  i64 n = 0;
  std::int32_t e = 0;
  bool b = false;
  switch (index) {
    case 0:
      if (!take_int(v, 0, 3, n)) return false;
      s.pad = static_cast<std::uint8_t>(n);
      return true;
    case 1:
      if (!take_int(v, -24, 24, n)) return false;
      s.notes[s.pad] = static_cast<std::int32_t>(n);
      chord_rebuild_if_last_held(s, g, effects);
      return true;
    case 2:
      if (!take_bool(v, b)) return false;
      s.sevenths[s.pad] = b ? 1 : 0;
      chord_rebuild_if_last_held(s, g, effects);
      return true;
    case 3:
      if (!take_enum(v, 2, e)) return false;
      s.sus[s.pad] = static_cast<std::uint8_t>(e);
      chord_rebuild_if_last_held(s, g, effects);
      return true;
    case 4:
      if (!take_bool(v, b)) return false;
      s.power[s.pad] = b ? 1 : 0;
      chord_rebuild_if_last_held(s, g, effects);
      return true;
    case 5:
      if (!take_bool(v, b)) return false;
      s.preview_chord = b;
      return true;
    case 6:
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = EffectReleaseNotes | EffectRepaint;
      return true;
    case 7:
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = EffectReleaseNotes | EffectRepaint;
      return true;
    case 8:
      if (!take_int(v, 0, 127, n)) return false;
      s.velocity = static_cast<std::uint8_t>(n);
      return true;
    default:
      return false;
  }
}

void chord_walk_attributes(InputState& state, AttributeVisitor& v) noexcept {
  if (!v.visit) return;
  auto& s = state.storage.chord_walk;
  AttributeDescriptor attrs[4] = {
    {"notes", AttributeType::Integer, 4, s.notes},
    {"sevenths", AttributeType::Byte, 4, s.sevenths},
    {"sus", AttributeType::Byte, 4, s.sus},
    {"power", AttributeType::Byte, 4, s.power}};
  for (auto& attr : attrs) {
    if (!v.visit(v.ctx, attr)) break;
  }
}

bool chord_walk_take_key_offset_request(InputState& state, std::int32_t& value) noexcept {
  auto& s = state.storage.chord_walk;
  if (!s.key_offset_request_pending) return false;
  s.key_offset_request_pending = false;
  value = kChordOffsetRequest;
  return true;
}

void set_chord_walk_pad_follow(InputState& state, bool enabled) noexcept {
  state.storage.chord_walk.follow_played_pad = enabled;
}

namespace {
const InputDescriptor kNoteWalkDescriptor{"note_walk", 1, kNoteWalkParams, 3, construct_note_walk_input, destroy,
                                          process_note_walk_input, reset_note_walk_input, set_note_walk_input_param,
                                          no_input_attributes};
const InputDescriptor kChordWalkDescriptor{"chord_walk", 1, kChordWalkParams, 9, construct_chord_walk_input, destroy,
                                           process_chord_walk_input, reset_chord_walk_input, set_chord_walk_input_param,
                                           chord_walk_attributes};
}  // namespace

const InputDescriptor& note_walk_input_descriptor() noexcept { return kNoteWalkDescriptor; }
const InputDescriptor& chord_walk_input_descriptor() noexcept { return kChordWalkDescriptor; }
}  // namespace orbit
