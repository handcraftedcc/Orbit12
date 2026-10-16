#include "matrix_input.hpp"
#include "../music.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

namespace orbit {
namespace {
constexpr std::uint8_t kPadCount = 12;
// Source temp_notes is NoteArray(length=6): one bass note may be inserted after a 5-note chord.
constexpr std::uint8_t kChordCapacity = 6;
// Relationship input marker for a note-off that is owed but not yet emitted
// because the batch off buffer was full. Pads are 0..11, so 0xFF is unused.
constexpr std::uint8_t kPendingRelease = 0xFF;
constexpr std::uint8_t kReleaseRepaint = EffectReleaseNotes | EffectRepaint;

constexpr const char* kSusOptions[] = {"OFF", "SUS2", "SUS4"};
constexpr const char* kBassOptions[] = {"NONE", "ROOT", "2ND", "LOW", "HIGH"};
constexpr const char* kSpreadOptions[] = {"TIGHT", "MED", "WIDE"};
constexpr const char* kResetOptions[] = {">", "DEG", "CHRD", "NOTE", "HALF"};
constexpr std::int32_t kIntMin = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kIntMax = std::numeric_limits<std::int32_t>::max();

// Colors are the source neo_pixels KEYCOLOR constants evaluated by rainbowio.colorwheel:
// BASE=128, ROOT=64, NAVPARMS=21 (chord), ENTER=42 (chord root).
constexpr RgbColor kBase{0, 126, 129};
constexpr RgbColor kRoot{63, 192, 0};
constexpr RgbColor kChord{192, 63, 0};
constexpr RgbColor kChordRoot{129, 126, 0};

const ParamSpec kParams[] = {
  {"pad", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(11), ParamValue::integer(1), "PAD", nullptr, 0, ParamPersist},
  {"note", ParamType::Integer, ParamValue::integer(1), ParamValue::integer(-24), ParamValue::integer(24), ParamValue::integer(1), "NOTE", nullptr, 0, ParamPersist},
  {"chord", ParamType::Boolean, ParamValue::boolean(true), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "CHORD", nullptr, 0, ParamPersist},
  {"seventh", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "7TH", nullptr, 0, ParamPersist},
  {"ninth", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "9TH", nullptr, 0, ParamPersist},
  {"inversion", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(3), ParamValue::integer(1), "INV", nullptr, 0, ParamPersist},
  {"sus", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), "SUS", kSusOptions, 3, ParamPersist},
  {"dim", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "DIM", nullptr, 0, ParamPersist},
  {"power", ParamType::Boolean, ParamValue::boolean(false), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), "PWR", nullptr, 0, ParamPersist},
  {"bass", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(4), ParamValue::integer(1), "BASS", kBassOptions, 5, ParamPersist},
  {"spread", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), "SPRD", kSpreadOptions, 3, ParamPersist},
  {"octave", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), "OCT", nullptr, 0, ParamPersist, GlobalBinding::Octave},
  {"key_offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), "PADOFS", nullptr, 0, ParamPersist, GlobalBinding::KeyOffset},
  {"velocity", ParamType::Integer, ParamValue::integer(127), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist},
  {"reset", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(4), ParamValue::integer(1), "RESET", kResetOptions, 5, ParamPersist}};

// Source LAYOUTS indexed by RESET value (index 0 is ">", which applies nothing).
constexpr std::int32_t kDefaultNotes[12] = {1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8};
constexpr std::uint8_t kDefaultChords[12] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
constexpr std::int32_t kDegreeNotes[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
constexpr std::int32_t kHalfNotes[12] = {1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6};
constexpr std::uint8_t kAllChords[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
constexpr std::uint8_t kNoChords[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kHalfChords[12] = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0};

const std::int32_t* layout_notes(std::uint8_t layout) noexcept {
  switch (layout) {
    case 1: return kDefaultNotes;
    case 2: case 3: return kDegreeNotes;
    case 4: return kHalfNotes;
    default: return nullptr;
  }
}
const std::uint8_t* layout_chords(std::uint8_t layout) noexcept {
  switch (layout) {
    case 1: return kDefaultChords;
    case 2: return kAllChords;
    case 3: return kNoChords;
    case 4: return kHalfChords;
    default: return nullptr;
  }
}

std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  auto quotient = value / divisor;
  if (value % divisor < 0) --quotient;
  return quotient;
}

std::size_t scale_index(const GlobalState& global) noexcept {
  return global.scale < music::kScaleLengths.size() ? static_cast<std::size_t>(global.scale) : std::size_t{1};
}

std::uint8_t clamp_midi(std::int64_t value) noexcept {
  if (value < 0) return 0;
  if (value > kMidiMax) return kMidiMax;
  return static_cast<std::uint8_t>(value);
}

// Source: note_for_degree. Pitch is clamped to MIDI range at this boundary;
// the source's bytearray note storage has no equivalent bound.
std::uint8_t pitch_for_degree(std::int64_t degree, const ProcessContext& c) noexcept {
  const auto scale = scale_index(c.global);
  const std::int64_t length = music::kScaleLengths[scale];
  const std::int64_t shifted = degree + c.global.key_offset;
  const std::int64_t octave = floor_div(shifted, length);
  const auto index = static_cast<std::size_t>(shifted - octave * length);
  return clamp_midi(static_cast<std::int64_t>(music::kScales[scale][index]) + c.global.key +
                    (octave + c.global.octave + 2) * 12);
}

// Index of a value within the physical pad map (source PADMAP.index).
std::uint8_t index_in_pad_map(std::uint8_t value) noexcept {
  for (std::uint8_t i = 0; i < kPadCount; ++i) {
    if (music::kPadMap[i] == value) return i;
  }
  return 0;  // Unreachable for values 0..11; validated by callers.
}

void set_colors(const MatrixInputState& s, const ProcessContext& c, InputView& view) noexcept {
  const auto length = static_cast<std::int64_t>(music::kScaleLengths[scale_index(c.global)]);
  for (std::uint8_t pad = 0; pad < kPadCount; ++pad) {
    const auto degree = static_cast<std::int64_t>(s.notes[pad]) - 1 + c.global.key_offset;
    const bool root = degree % length == 0;
    RgbColor color = s.chords[pad] ? kChord : kBase;
    if (root) color = s.chords[pad] ? kChordRoot : kRoot;
    view.pad_colors[index_in_pad_map(pad)] = color;
  }
}

void push_chord_note(MatrixInputState& s, std::uint8_t pitch) noexcept {
  if (s.temp_count < kChordCapacity) {
    s.temp_notes[s.temp_count++] = pitch;
  } else {
    ++s.output_overflow;
  }
}

std::uint8_t raise_octave(std::uint8_t pitch) noexcept {
  return clamp_midi(static_cast<std::int64_t>(pitch) + 12);
}

// Source: build_pad_notes. Order of operations is dim, inversion, spread, bass.
void build_pad_notes(MatrixInputState& s, std::uint8_t pad, const ProcessContext& c) noexcept {
  s.temp_count = 0;
  const std::int64_t root = static_cast<std::int64_t>(s.notes[pad]) - 1;
  if (!s.chords[pad]) {
    push_chord_note(s, pitch_for_degree(root, c));
    return;
  }

  std::int64_t degrees[5]{};
  std::uint8_t degree_count = 0;
  degrees[degree_count++] = root;
  if (s.sus[pad] == 1) {
    degrees[degree_count++] = root + 1;
  } else if (s.sus[pad] == 2) {
    degrees[degree_count++] = root + 3;
  } else if (!s.power[pad]) {
    degrees[degree_count++] = root + 2;
  }
  degrees[degree_count++] = root + 4;
  if (s.sevenths[pad]) degrees[degree_count++] = root + 6;
  if (s.ninths[pad]) degrees[degree_count++] = root + 8;
  for (std::uint8_t i = 0; i < degree_count; ++i) push_chord_note(s, pitch_for_degree(degrees[i], c));

  // Bass uses the root before any dim/inversion/spread change.
  const std::uint8_t root_pitch = s.temp_notes[0];

  if (s.dim[pad]) {
    if (s.sus[pad] == 0 && s.temp_count > 1) s.temp_notes[1] = clamp_midi(s.temp_notes[1] - 1);
    if (s.temp_count > 2) s.temp_notes[2] = clamp_midi(s.temp_notes[2] - 1);
  }

  const auto max_inversion = static_cast<std::uint8_t>(s.temp_count - 1);
  const auto inversion = std::min(s.inversions[pad], max_inversion);
  for (std::uint8_t i = 0; i < inversion; ++i) s.temp_notes[i] = raise_octave(s.temp_notes[i]);

  if (s.spread_mode != 0) {
    const auto last = static_cast<std::uint8_t>(s.temp_count - 1);
    s.temp_notes[last] = raise_octave(s.temp_notes[last]);
    if (s.spread_mode == 2 && s.temp_count > 2) {
      const auto second_last = static_cast<std::uint8_t>(s.temp_count - 2);
      s.temp_notes[second_last] = raise_octave(s.temp_notes[second_last]);
    }
  }

  if (s.bass_mode != 0) {
    std::int64_t bass = 0;
    bool has_bass = true;
    if (s.bass_mode == 1) {
      bass = static_cast<std::int64_t>(root_pitch) - 12;
    } else if (s.bass_mode == 2) {
      if (s.temp_count > 1) {
        bass = static_cast<std::int64_t>(s.temp_notes[1]) - 12;
      } else {
        has_bass = false;
      }
    } else if (s.bass_mode == 3 || s.bass_mode == 4) {
      std::uint8_t extreme = s.temp_notes[0];
      for (std::uint8_t i = 1; i < s.temp_count; ++i) {
        const auto note = s.temp_notes[i];
        if ((s.bass_mode == 3 && note < extreme) || (s.bass_mode == 4 && note > extreme)) extreme = note;
      }
      bass = static_cast<std::int64_t>(extreme) - 12;
    } else {
      has_bass = false;
    }
    if (has_bass && s.temp_count < kChordCapacity) {
      for (std::uint8_t i = s.temp_count; i > 0; --i) s.temp_notes[i] = s.temp_notes[i - 1];
      s.temp_notes[0] = clamp_midi(bass);
      ++s.temp_count;
    }
  }
}

void remove_entry(MatrixRelationships& r, std::uint8_t index) noexcept {
  for (std::uint8_t j = static_cast<std::uint8_t>(index + 1); j < r.count; ++j) {
    r.input[j - 1] = r.input[j];
    r.output[j - 1] = r.output[j];
  }
  --r.count;
}

// True when a sounding relationship from a held (non-pending) pad produces this pitch.
bool active_holds(const MatrixRelationships& r, std::uint8_t pitch) noexcept {
  for (std::uint8_t i = 0; i < r.count; ++i) {
    if (r.output[i] == pitch && r.input[i] != kPendingRelease) return true;
  }
  return false;
}

// Emits owed note-offs in table order while the off buffer has room. Entries
// that cannot be emitted stay pending and are retried by the next process call.
// An owed off is dropped only when an active relationship still sounds the same pitch,
// matching source has_out_note semantics.
void drain_pending_releases(MatrixInputState& s, NoteBatch& out) noexcept {
  auto& r = s.relationships;
  std::uint8_t i = 0;
  while (i < r.count) {
    if (r.input[i] != kPendingRelease) {
      ++i;
      continue;
    }
    const auto pitch = r.output[i];
    if (active_holds(r, pitch)) {
      remove_entry(r, i);
      continue;
    }
    if (out.off.count >= out.off.capacity()) {
      ++s.output_overflow;
      ++i;
      continue;
    }
    // Capacity was checked above, so append cannot fail.
    (void)out.off.append(Note{pitch, 0, 0});
    remove_entry(r, i);
  }
}

// Source: process note_offs. Every relationship of the pad is marked owed, then drained.
void release_pad(MatrixInputState& s, std::uint8_t pad, NoteBatch& out) noexcept {
  auto& r = s.relationships;
  for (std::uint8_t i = 0; i < r.count; ++i) {
    if (r.input[i] == pad) r.input[i] = kPendingRelease;
  }
  drain_pending_releases(s, out);
}

// Source: process note_ons. A retrigger emits an off before the on; capacity is
// checked before any state changes, so a note that cannot be tracked is never sounded.
void press_pad(MatrixInputState& s, std::uint8_t pad, const ProcessContext& c, NoteBatch& out) noexcept {
  if (s.follow_played_pad) s.pad = pad;
  build_pad_notes(s, pad, c);
  for (std::uint8_t j = 0; j < s.temp_count; ++j) {
    const auto pitch = s.temp_notes[j];
    if (s.relationships.count >= MatrixRelationships::kCapacity) {
      ++s.relationship_overflow;
      break;
    }
    if (out.on.count >= kPolyphony) {
      ++s.output_overflow;
      break;
    }
    const bool retrigger = s.relationships.has_output(pitch);
    if (retrigger && out.off.count >= out.off.capacity()) {
      ++s.output_overflow;
      continue;
    }
    if (retrigger) (void)out.off.append(Note{pitch, 0, 0});
    (void)s.relationships.add(pad, pitch);
    (void)out.on.append(Note{pitch, s.velocity, 0});
  }
}

void attributes(InputState& state, AttributeVisitor& v) noexcept {
  if (!v.visit) return;
  auto& s = state.storage.matrix;
  AttributeDescriptor attrs[8] = {
    {"notes", AttributeType::Integer, 12, s.notes},
    {"chords", AttributeType::Byte, 12, s.chords},
    {"sevenths", AttributeType::Byte, 12, s.sevenths},
    {"ninths", AttributeType::Byte, 12, s.ninths},
    {"inversions", AttributeType::Byte, 12, s.inversions},
    {"sus", AttributeType::Byte, 12, s.sus},
    {"power", AttributeType::Byte, 12, s.power},
    {"dim", AttributeType::Byte, 12, s.dim}};
  for (auto& attr : attrs) {
    if (!v.visit(v.ctx, attr)) break;
  }
}

void destroy(InputState&) noexcept {}

bool take_int(const ParamValue& v, std::int32_t lo, std::int32_t hi, std::int32_t& out) noexcept {
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

const InputDescriptor kDescriptor{"matrix", 1, kParams, 15, construct_matrix_input, destroy,
                                  process_matrix_input, reset_matrix_input, set_matrix_input_param, attributes};
static_assert(std::is_trivially_destructible<MatrixInputState>::value,
              "Input-state union members must have trivial destruction");
}  // namespace

bool MatrixRelationships::add(std::uint8_t in, std::uint8_t out) noexcept {
  if (count >= kCapacity) return false;
  input[count] = in;
  output[count] = out;
  ++count;
  return true;
}

// Precondition: cap >= kCapacity, so every matching entry is reported.
std::uint8_t MatrixRelationships::remove_all(std::uint8_t in, std::uint8_t* removed, std::uint8_t cap) noexcept {
  std::uint8_t n = 0;
  std::uint8_t i = 0;
  while (i < count) {
    if (input[i] == in) {
      if (n < cap) removed[n++] = output[i];
      remove_entry(*this, i);
    } else {
      ++i;
    }
  }
  return n;
}

bool MatrixRelationships::has_output(std::uint8_t out) const noexcept {
  for (std::uint8_t i = 0; i < count; ++i) {
    if (output[i] == out) return true;
  }
  return false;
}

void construct_matrix_input(InputState& s) noexcept { new (&s.storage.matrix) MatrixInputState{}; }

void process_matrix_input(InputState& state, const ProcessContext& c, const InputFrame& frame, NoteBatch& out,
                          InputView& view) noexcept {
  auto& s = state.storage.matrix;
  out = NoteBatch{};
  set_colors(s, c, view);
  // Owed note-offs from earlier calls go first, so releases are never abandoned.
  drain_pending_releases(s, out);

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
  // Source processes all note-offs before note-ons within a frame.
  for (std::uint16_t i = 0; i < up_count; ++i) release_pad(s, index_in_pad_map(ups[i]), out);
  for (std::uint16_t i = 0; i < down_count; ++i) press_pad(s, index_in_pad_map(downs[i]), c, out);
}

void reset_matrix_input(InputState& state, ResetReason r) noexcept {
  if (r == ResetReason::Stop || r == ResetReason::SceneChange || r == ResetReason::Panic || r == ResetReason::Init) {
    auto& s = state.storage.matrix;
    s.relationships.clear();
    s.temp_count = 0;
  }
}

bool matrix_release_pending(const InputState& state) noexcept {
  const auto& r = state.storage.matrix.relationships;
  for (std::uint8_t i = 0; i < r.count; ++i) {
    if (r.input[i] == kPendingRelease) return true;
  }
  return false;
}

bool set_matrix_input_param(InputState& state, std::uint8_t index, ParamValue v, const GlobalState&,
                            std::uint8_t& effects) noexcept {
  auto& s = state.storage.matrix;
  effects = EffectNone;
  std::int32_t n = 0;
  bool b = false;
  switch (index) {
    case 0:
      if (!take_int(v, 0, kPadCount - 1, n)) return false;
      s.pad = static_cast<std::uint8_t>(n);
      return true;
    case 1:
      if (!take_int(v, -24, 24, n)) return false;
      s.notes[s.pad] = n;
      effects = EffectRepaint;
      return true;
    case 2:
      if (!take_bool(v, b)) return false;
      s.chords[s.pad] = b ? 1 : 0;
      effects = EffectRepaint;
      return true;
    case 3:
      if (!take_bool(v, b)) return false;
      s.sevenths[s.pad] = b ? 1 : 0;
      return true;
    case 4:
      if (!take_bool(v, b)) return false;
      s.ninths[s.pad] = b ? 1 : 0;
      return true;
    case 5:
      if (!take_int(v, 0, 3, n)) return false;
      s.inversions[s.pad] = static_cast<std::uint8_t>(n);
      return true;
    case 6:
      if (!take_enum(v, 2, n)) return false;
      s.sus[s.pad] = static_cast<std::uint8_t>(n);
      return true;
    case 7:
      if (!take_bool(v, b)) return false;
      s.dim[s.pad] = b ? 1 : 0;
      return true;
    case 8:
      if (!take_bool(v, b)) return false;
      s.power[s.pad] = b ? 1 : 0;
      return true;
    case 9:
      if (!take_enum(v, 4, n)) return false;
      s.bass_mode = static_cast<std::uint8_t>(n);
      return true;
    case 10:
      if (!take_enum(v, 2, n)) return false;
      s.spread_mode = static_cast<std::uint8_t>(n);
      return true;
    case 11:
      // Octave is a global binding; the controller applies it and this releases notes.
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = kReleaseRepaint;
      return true;
    case 12:
      if (!take_int(v, kIntMin, kIntMax, n)) return false;
      effects = kReleaseRepaint;
      return true;
    case 13:
      if (!take_int(v, 0, 127, n)) return false;
      s.velocity = static_cast<std::uint8_t>(n);
      return true;
    case 14:
      if (!take_enum(v, 4, n)) return false;
      s.reset_value = static_cast<std::uint8_t>(n);
      return true;
    default:
      return false;
  }
}

void set_matrix_pad_follow(InputState& state, bool enabled) noexcept { state.storage.matrix.follow_played_pad = enabled; }

void arm_matrix_reset(InputState& state) noexcept { state.storage.matrix.reset_armed = true; }

// Source reset_layout: a nonzero value applies the preset and clears per-pad modifiers.
void finish_matrix_reset(InputState& state, std::uint8_t& effects) noexcept {
  auto& s = state.storage.matrix;
  effects = EffectNone;
  const auto* notes = layout_notes(s.reset_value);
  const auto* chords = layout_chords(s.reset_value);
  if (notes && chords) {
    for (std::uint8_t i = 0; i < kPadCount; ++i) {
      s.notes[i] = notes[i];
      s.chords[i] = chords[i];
      s.sevenths[i] = 0;
      s.ninths[i] = 0;
      s.inversions[i] = 0;
      s.sus[i] = 0;
      s.power[i] = 0;
      s.dim[i] = 0;
    }
    effects = EffectRepaint;
  }
  s.reset_value = 0;
  s.reset_armed = false;
}

void matrix_arm_reset(InputState& s) noexcept { arm_matrix_reset(s); }
void matrix_finish_reset(InputState& s, std::uint8_t& effects) noexcept { finish_matrix_reset(s, effects); }
void matrix_follow_played_pad(InputState& s, bool enabled) noexcept { set_matrix_pad_follow(s, enabled); }

const InputDescriptor& matrix_input_descriptor() noexcept { return kDescriptor; }
}  // namespace orbit
