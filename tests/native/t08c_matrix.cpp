// Source oracle: circuitpython-source/src/apps/Orion/inputmodules/matrix.py
// Expected pitches assume GlobalState defaults (key C, Major, octave 3, offset 0)
// unless a case sets otherwise. Pad p is driven by physical key music::kPadMap[p].
#include "modules/core/matrix/matrix_input.hpp"
#include "modules/common/input_registry.hpp"
#include "domain/music.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace {
using namespace orbit;

int g_failures = 0;
#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);   \
      ++g_failures;                                                                   \
    }                                                                                 \
  } while (0)

struct Rng {
  std::uint32_t value = 0;
};
std::uint32_t draw(void* p) noexcept { return ++static_cast<Rng*>(p)->value; }

ParamValue I(std::int32_t v) { return ParamValue::integer(v); }
ParamValue B(bool v) { return ParamValue::boolean(v); }
ParamValue E(std::int32_t v) { return ParamValue(ParamType::Enumeration, ParamValue::Scalar(v)); }

std::uint8_t key(std::uint8_t pad) { return music::kPadMap[pad]; }

InputFrame frame(std::initializer_list<KeyEvent> events) {
  InputFrame f{};
  for (const auto& e : events) CHECK(f.keys.append(e) == PushResult::Added);
  return f;
}

template <std::size_t N>
bool same(const NoteBuffer& b, const std::uint8_t (&expected)[N]) {
  if (b.count != N) return false;
  for (std::size_t i = 0; i < N; ++i) {
    if (b.items[i].pitch != expected[i]) return false;
  }
  return true;
}

bool same_color(RgbColor a, RgbColor b) { return a.red == b.red && a.green == b.green && a.blue == b.blue; }

// Harness owns every object that ProcessContext references, so contexts never dangle.
struct Harness {
  GlobalState global{};
  Rng rng{};
  RandomSource random{&rng, draw};
  InputState state{};
  NoteBatch out{};
  InputView view{};
  // Last event per pitch across all batches: true means the most recent event was a note-on.
  bool last_on[128]{};

  Harness() { construct_matrix_input(state); }

  const InputDescriptor& d() const { return matrix_input_descriptor(); }
  const MatrixInputState& m() const { return state.storage.matrix; }

  ProcessContext ctx() { return ProcessContext{{0, 0}, global, {}, random}; }

  void step(const InputFrame& f) {
    auto c = ctx();
    d().process(state, c, f, out, view);
    for (std::uint16_t i = 0; i < out.off.count; ++i) last_on[out.off.items[i].pitch] = false;
    for (std::uint16_t i = 0; i < out.on.count; ++i) last_on[out.on.items[i].pitch] = true;
  }
  void press(std::uint8_t k) { step(frame({{k, true, 0}})); }
  void release(std::uint8_t k) { step(frame({{k, false, 0}})); }
  void set(std::uint8_t index, ParamValue v, std::uint8_t* effects = nullptr) {
    std::uint8_t e = 0xEE;
    CHECK(d().set_param(state, index, v, global, e));
    if (effects) *effects = e;
  }
  bool rejects(std::uint8_t index, ParamValue v) {
    std::uint8_t e = 0xEE;
    const bool ok = d().set_param(state, index, v, global, e);
    return !ok && e == EffectNone;
  }
  void select_pad(std::uint8_t pad) { set(0, I(pad)); }
  bool any_last_on() const {
    for (bool v : last_on) {
      if (v) return true;
    }
    return false;
  }
};

// Snapshot of the eight saved attributes, flattened for round-trip restore.
struct Snapshot {
  std::uint8_t attr_count = 0;
  const char* names[8]{};
  AttributeType types[8]{};
  std::uint16_t lengths[8]{};
  std::int64_t values[8][12]{};
};

bool record_attr(void* context, AttributeDescriptor& a) {
  auto& snap = *static_cast<Snapshot*>(context);
  const auto i = snap.attr_count++;
  snap.names[i] = a.name;
  snap.types[i] = a.type;
  snap.lengths[i] = a.element_count;
  for (std::uint16_t j = 0; j < a.element_count; ++j) {
    snap.values[i][j] = a.type == AttributeType::Integer ? static_cast<std::int64_t>(static_cast<std::int32_t*>(a.values)[j])
                                                         : static_cast<std::int64_t>(static_cast<std::uint8_t*>(a.values)[j]);
  }
  return true;
}

bool restore_attr(void* context, AttributeDescriptor& a) {
  auto& snap = *static_cast<Snapshot*>(context);
  const auto i = snap.attr_count++;
  CHECK(std::strcmp(a.name, snap.names[i]) == 0);
  for (std::uint16_t j = 0; j < a.element_count; ++j) {
    if (a.type == AttributeType::Integer) {
      static_cast<std::int32_t*>(a.values)[j] = static_cast<std::int32_t>(snap.values[i][j]);
    } else {
      static_cast<std::uint8_t*>(a.values)[j] = static_cast<std::uint8_t>(snap.values[i][j]);
    }
  }
  return true;
}

void schema_and_registry() {
  const auto& d = matrix_input_descriptor();
  CHECK(d.id && std::strcmp(d.id, "matrix") == 0);
  CHECK(d.version == 1);
  CHECK(d.param_count == 15);

  // Parameter order, labels and global bindings follow the source Matrix parm list.
  const char* names[15] = {"pad", "note", "chord", "seventh", "ninth", "inversion", "sus", "dim",
                           "power", "bass", "spread", "octave", "key_offset", "velocity", "reset"};
  const char* labels[15] = {"PAD", "NOTE", "CHORD", "7TH", "9TH", "INV", "SUS", "DIM",
                            "PWR", "BASS", "SPRD", "OCT", "PADOFS", "VEL", "RESET"};
  for (std::uint8_t i = 0; i < 15; ++i) {
    CHECK(std::strcmp(d.params[i].name, names[i]) == 0);
    CHECK(std::strcmp(d.params[i].display_label, labels[i]) == 0);
    const auto expected = i == 11 ? GlobalBinding::Octave : (i == 12 ? GlobalBinding::KeyOffset : GlobalBinding::None);
    CHECK(d.params[i].global_binding == expected);
  }

  // Source defaults and ranges.
  CHECK(d.params[0].default_value.value.integer == 0 && d.params[0].minimum.value.integer == 0 && d.params[0].maximum.value.integer == 11);
  CHECK(d.params[1].default_value.value.integer == 1 && d.params[1].minimum.value.integer == -24 && d.params[1].maximum.value.integer == 24);
  CHECK(d.params[2].default_value.value.boolean == true);
  CHECK(d.params[3].default_value.value.boolean == false && d.params[4].default_value.value.boolean == false);
  CHECK(d.params[5].minimum.value.integer == 0 && d.params[5].maximum.value.integer == 3);
  CHECK(d.params[6].option_count == 3 && std::strcmp(d.params[6].options[1], "SUS2") == 0 && std::strcmp(d.params[6].options[2], "SUS4") == 0);
  CHECK(d.params[7].default_value.value.boolean == false && d.params[8].default_value.value.boolean == false);
  CHECK(d.params[9].option_count == 5 && std::strcmp(d.params[9].options[4], "HIGH") == 0);
  CHECK(d.params[10].option_count == 3 && std::strcmp(d.params[10].options[2], "WIDE") == 0);
  CHECK(d.params[11].default_value.value.integer == 3);
  CHECK(d.params[12].default_value.value.integer == 0);
  CHECK(d.params[13].default_value.value.integer == 127 && d.params[13].minimum.value.integer == 0 && d.params[13].maximum.value.integer == 127);
  CHECK(d.params[14].option_count == 5 && std::strcmp(d.params[14].options[0], ">") == 0 && std::strcmp(d.params[14].options[4], "HALF") == 0);

  Harness h;
  CHECK(h.m().pad == 0 && h.m().bass_mode == 0 && h.m().spread_mode == 0 && h.m().velocity == 127);
  const std::int32_t default_notes[12] = {1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8};
  const std::uint8_t default_chords[12] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
  for (std::uint8_t i = 0; i < 12; ++i) {
    CHECK(h.m().notes[i] == default_notes[i]);
    CHECK(h.m().chords[i] == default_chords[i]);
    CHECK(h.m().sevenths[i] == 0 && h.m().ninths[i] == 0 && h.m().inversions[i] == 0);
    CHECK(h.m().sus[i] == 0 && h.m().power[i] == 0 && h.m().dim[i] == 0);
  }

  // Eight saved attributes, 96 scalars, in source save_attrs order.
  Snapshot snap{};
  AttributeVisitor visitor{&snap, record_attr};
  d.attributes(h.state, visitor);
  CHECK(snap.attr_count == 8);
  const char* saved[8] = {"notes", "chords", "sevenths", "ninths", "inversions", "sus", "power", "dim"};
  std::uint32_t scalars = 0;
  for (std::uint8_t a = 0; a < snap.attr_count; ++a) {
    CHECK(std::strcmp(snap.names[a], saved[a]) == 0);
    CHECK(snap.types[a] == (a == 0 ? AttributeType::Integer : AttributeType::Byte));
    scalars += snap.lengths[a];
  }
  CHECK(scalars == 96);

  CHECK(find_input_module("matrix") == &matrix_input_descriptor());
  CHECK(input_module_count() == 6 && input_module_at(3) == &matrix_input_descriptor());
  CHECK(find_input_module("phraser") == nullptr);
}

void defaults_and_pad_releases() {
  // Expected per-pad default outputs (source default NOTE/CHORD table).
  const std::uint8_t p0[] = {60, 64, 67}, p1[] = {60}, p2[] = {62}, p3[] = {64, 67, 71};
  const std::uint8_t p4[] = {64}, p5[] = {65}, p6[] = {67, 71, 74}, p7[] = {67};
  const std::uint8_t p8[] = {69}, p9[] = {71, 74, 77}, p10[] = {71}, p11[] = {72};
  const std::uint8_t* expected[12] = {p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11};
  const std::uint8_t counts[12] = {3, 1, 1, 3, 1, 1, 3, 1, 1, 3, 1, 1};

  Harness h;
  for (std::uint8_t pad = 0; pad < 12; ++pad) {
    h.press(key(pad));
    CHECK(h.out.on.count == counts[pad]);
    for (std::uint8_t i = 0; i < counts[pad]; ++i) {
      CHECK(h.out.on.items[i].pitch == expected[pad][i]);
      CHECK(h.out.on.items[i].velocity == 127 && h.out.on.items[i].channel == 0);
    }
    h.release(key(pad));
    CHECK(h.out.off.count == counts[pad]);
    for (std::uint8_t i = 0; i < counts[pad]; ++i) CHECK(h.out.off.items[i].pitch == expected[pad][i]);
    CHECK(h.out.on.count == 0);
  }
  CHECK(!h.any_last_on());

  // Pad colors: chord root, root, base, chord; indexed by physical key position.
  h.step(InputFrame{});
  CHECK(same_color(h.view.pad_colors[9], RgbColor{129, 126, 0}));   // pad 0: chord root
  CHECK(same_color(h.view.pad_colors[10], RgbColor{63, 192, 0}));   // pad 1: root
  CHECK(same_color(h.view.pad_colors[11], RgbColor{0, 126, 129}));  // pad 2: base
  CHECK(same_color(h.view.pad_colors[6], RgbColor{192, 63, 0}));    // pad 3: chord
}

void chord_modifiers_on_pad0() {
  Harness h;
  auto reset_pad0 = [&h]() {
    h.select_pad(0);
    h.set(2, B(true));
    h.set(3, B(false));
    h.set(4, B(false));
    h.set(5, I(0));
    h.set(6, E(0));
    h.set(7, B(false));
    h.set(8, B(false));
  };
  auto check_case = [&h](std::uint8_t k, const std::uint8_t* expected, std::uint8_t n) {
    h.press(k);
    CHECK(h.out.on.count == n);
    for (std::uint8_t i = 0; i < n && i < h.out.on.count; ++i) CHECK(h.out.on.items[i].pitch == expected[i]);
    h.release(k);
  };
  const std::uint8_t k = key(0);

  reset_pad0(); h.set(2, B(false)); { const std::uint8_t e[] = {60}; check_case(k, e, 1); }
  reset_pad0(); h.set(6, E(1)); { const std::uint8_t e[] = {60, 62, 67}; check_case(k, e, 3); }
  reset_pad0(); h.set(6, E(2)); { const std::uint8_t e[] = {60, 65, 67}; check_case(k, e, 3); }
  reset_pad0(); h.set(8, B(true)); { const std::uint8_t e[] = {60, 67}; check_case(k, e, 2); }
  reset_pad0(); h.set(6, E(1)); h.set(8, B(true)); { const std::uint8_t e[] = {60, 62, 67}; check_case(k, e, 3); }
  reset_pad0(); h.set(3, B(true)); h.set(4, B(true)); { const std::uint8_t e[] = {60, 64, 67, 71, 74}; check_case(k, e, 5); }
  reset_pad0(); h.set(5, I(1)); { const std::uint8_t e[] = {72, 64, 67}; check_case(k, e, 3); }
  reset_pad0(); h.set(5, I(2)); { const std::uint8_t e[] = {72, 76, 67}; check_case(k, e, 3); }
  // Inversion is clamped to length-1: a 5-note chord takes 3, a triad takes 2.
  reset_pad0(); h.set(3, B(true)); h.set(4, B(true)); h.set(5, I(3)); { const std::uint8_t e[] = {72, 76, 79, 71, 74}; check_case(k, e, 5); }
  reset_pad0(); h.set(5, I(3)); { const std::uint8_t e[] = {72, 76, 67}; check_case(k, e, 3); }
  reset_pad0(); h.set(7, B(true)); { const std::uint8_t e[] = {60, 63, 66}; check_case(k, e, 3); }
  // Dim lowers only the third when sus is set; sus takes the second slot.
  reset_pad0(); h.set(7, B(true)); h.set(6, E(1)); { const std::uint8_t e[] = {60, 62, 66}; check_case(k, e, 3); }
  reset_pad0(); h.set(7, B(true)); h.set(8, B(true)); { const std::uint8_t e[] = {60, 66}; check_case(k, e, 2); }
  // Sus takes precedence over power, so sus2 with power keeps the second.
  reset_pad0(); h.set(6, E(1)); h.set(8, B(true)); { const std::uint8_t e[] = {60, 62, 67}; check_case(k, e, 3); }
  reset_pad0();

  // Modifiers are per pad: pad 5 changes leave pad 0 untouched.
  h.select_pad(5);
  h.set(2, B(false));
  CHECK(h.m().chords[5] == 0 && h.m().chords[0] == 1);
  { const std::uint8_t e[] = {65}; h.press(key(5)); CHECK(same(h.out.on, e)); h.release(key(5)); }
  { const std::uint8_t e[] = {60, 64, 67}; h.press(key(0)); CHECK(same(h.out.on, e)); h.release(key(0)); }
  h.set(2, B(true));
}

void bass_spread_and_order() {
  Harness h;
  auto reset_global = [&h]() {
    h.set(9, E(0));
    h.set(10, E(0));
    h.select_pad(0);
    h.set(7, B(false));
    h.set(5, I(0));
    h.set(6, E(0));
    h.set(8, B(false));
  };
  const std::uint8_t k0 = key(0);
  auto expect_press = [&h](std::uint8_t k, const std::uint8_t* e, std::uint8_t n) {
    h.press(k);
    CHECK(h.out.on.count == n);
    for (std::uint8_t i = 0; i < n && i < h.out.on.count; ++i) CHECK(h.out.on.items[i].pitch == e[i]);
    h.release(k);
  };

  h.set(10, E(1)); { const std::uint8_t e[] = {60, 64, 79}; expect_press(k0, e, 3); }
  h.set(10, E(2)); { const std::uint8_t e[] = {60, 76, 79}; expect_press(k0, e, 3); }
  h.set(7, B(true)); { const std::uint8_t e[] = {60, 75, 78}; expect_press(k0, e, 3); }  // dim before spread
  reset_global();

  h.set(9, E(1)); { const std::uint8_t e[] = {48, 60, 64, 67}; expect_press(k0, e, 4); }
  h.set(9, E(2)); { const std::uint8_t e[] = {52, 60, 64, 67}; expect_press(k0, e, 4); }
  h.set(9, E(3)); { const std::uint8_t e[] = {48, 60, 64, 67}; expect_press(k0, e, 4); }
  h.set(9, E(4)); { const std::uint8_t e[] = {55, 60, 64, 67}; expect_press(k0, e, 4); }
  // ROOT bass uses the root before inversion moves the lowest note up.
  h.set(9, E(1)); h.set(5, I(1)); { const std::uint8_t e[] = {48, 72, 64, 67}; expect_press(k0, e, 4); }
  reset_global();
  // Two-note power chord: 2ND bass takes the fifth down an octave.
  h.set(9, E(2)); h.set(8, B(true)); { const std::uint8_t e[] = {55, 60, 67}; expect_press(k0, e, 3); }
  reset_global();
  // Non-chord pad does not receive bass or spread.
  h.set(9, E(1)); h.set(10, E(2)); { const std::uint8_t e[] = {60}; expect_press(key(1), e, 1); }
  reset_global();
}

void velocity_and_global_mapping() {
  Harness h;
  h.set(13, I(90));
  h.press(key(0));
  CHECK(h.out.on.count == 3);
  for (std::uint8_t i = 0; i < h.out.on.count; ++i) CHECK(h.out.on.items[i].velocity == 90);
  h.release(key(0));

  h.global.key = 2;  // D
  h.press(key(0));
  { const std::uint8_t e[] = {62, 66, 69}; CHECK(same(h.out.on, e)); }
  h.release(key(0));
  h.global.key = 0;

  std::uint8_t effects = 0;
  h.set(11, I(4), &effects);
  CHECK((effects & EffectReleaseNotes) != 0 && (effects & EffectRepaint) != 0);
  h.global.octave = 4;
  h.press(key(0));
  { const std::uint8_t e[] = {72, 76, 79}; CHECK(same(h.out.on, e)); }
  h.release(key(0));
  h.global.octave = 3;

  // Key offset -1 moves the degrees below C: degree -1 is B3 (59).
  h.set(12, I(-1), &effects);
  CHECK((effects & EffectReleaseNotes) != 0 && (effects & EffectRepaint) != 0);
  h.global.key_offset = -1;
  h.set(2, B(false));
  h.press(key(0));
  { const std::uint8_t e[] = {59}; CHECK(same(h.out.on, e)); }
  h.release(key(0));
  h.set(2, B(true));
  h.press(key(0));
  { const std::uint8_t e[] = {59, 62, 65}; CHECK(same(h.out.on, e)); }
  h.release(key(0));
  h.global.key_offset = 0;
  h.set(12, I(0));

  // Extreme globals saturate to MIDI range without overflow.
  h.global.octave = std::numeric_limits<std::int32_t>::max();
  h.set(2, B(false));
  h.press(key(1));
  { const std::uint8_t e[] = {127}; CHECK(same(h.out.on, e)); }
  h.release(key(1));
  h.global.octave = std::numeric_limits<std::int32_t>::min();
  h.press(key(1));
  { const std::uint8_t e[] = {0}; CHECK(same(h.out.on, e)); }
  h.release(key(1));
  h.global.octave = 3;
  h.global.key_offset = std::numeric_limits<std::int32_t>::max();
  h.press(key(1));
  { const std::uint8_t e[] = {127}; CHECK(same(h.out.on, e)); }
  h.release(key(1));
  h.global.key_offset = 0;
  h.set(2, B(true));
  h.set(13, I(127));
}

void follow_played_pad() {
  Harness h;
  matrix_follow_played_pad(h.state, true);
  h.press(key(8));
  CHECK(h.m().pad == 8);
  { const std::uint8_t e[] = {69}; CHECK(same(h.out.on, e)); }
  h.release(key(8));

  matrix_follow_played_pad(h.state, false);
  h.press(key(4));
  CHECK(h.m().pad == 8);
  h.release(key(4));
}

void reset_layouts() {
  Harness h;
  h.select_pad(3);
  h.set(1, I(5));
  h.set(2, B(false));
  h.set(3, B(true));
  h.set(4, B(true));
  h.set(5, I(2));
  h.set(6, E(1));
  h.set(7, B(true));
  h.set(8, B(true));
  h.set(9, E(1));

  // RESET ">" arms and exits without applying anything.
  matrix_arm_reset(h.state);
  CHECK(h.m().reset_armed);
  h.set(14, E(0));
  std::uint8_t effects = 0xEE;
  matrix_finish_reset(h.state, effects);
  CHECK(effects == EffectNone);
  CHECK(!h.m().reset_armed && h.m().reset_value == 0);
  CHECK(h.m().notes[3] == 5 && h.m().chords[3] == 0 && h.m().sevenths[3] == 1);

  // DEG applies the default layout and clears modifiers on every pad.
  matrix_arm_reset(h.state);
  h.set(14, E(1));
  matrix_finish_reset(h.state, effects);
  CHECK(effects == EffectRepaint);
  const std::int32_t default_notes[12] = {1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8};
  const std::uint8_t default_chords[12] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
  for (std::uint8_t i = 0; i < 12; ++i) {
    CHECK(h.m().notes[i] == default_notes[i] && h.m().chords[i] == default_chords[i]);
    CHECK(h.m().sevenths[i] == 0 && h.m().ninths[i] == 0 && h.m().inversions[i] == 0);
    CHECK(h.m().sus[i] == 0 && h.m().power[i] == 0 && h.m().dim[i] == 0);
  }
  CHECK(h.m().pad == 3 && h.m().bass_mode == 1);  // Pad selection and bass survive the layout.

  // CHRD: degree notes, every pad a chord.
  h.set(14, E(2));
  matrix_finish_reset(h.state, effects);
  for (std::uint8_t i = 0; i < 12; ++i) CHECK(h.m().notes[i] == i + 1 && h.m().chords[i] == 1);

  // NOTE: degree notes, no chords.
  h.set(14, E(3));
  matrix_finish_reset(h.state, effects);
  for (std::uint8_t i = 0; i < 12; ++i) CHECK(h.m().notes[i] == i + 1 && h.m().chords[i] == 0);

  // HALF: two repeated octaves, first six pads chords.
  h.set(14, E(4));
  matrix_finish_reset(h.state, effects);
  const std::int32_t half_notes[12] = {1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6};
  for (std::uint8_t i = 0; i < 12; ++i) {
    CHECK(h.m().notes[i] == half_notes[i] && h.m().chords[i] == (i < 6 ? 1 : 0));
  }
  CHECK(h.m().reset_value == 0);

  CHECK(h.rejects(14, E(5)));
  CHECK(h.rejects(14, I(1)));
}

void parameter_validation_and_effects() {
  Harness h;
  CHECK(h.rejects(0, I(12)));
  CHECK(h.rejects(1, I(25)));
  CHECK(h.rejects(1, B(true)));
  CHECK(h.rejects(3, I(1)));
  CHECK(h.rejects(5, I(4)));
  CHECK(h.rejects(6, E(3)));
  CHECK(h.rejects(9, E(5)));
  CHECK(h.rejects(10, E(3)));
  CHECK(h.rejects(13, I(128)));
  CHECK(h.rejects(14, E(5)));
  CHECK(h.rejects(15, I(0)));
  CHECK(h.m().notes[0] == 1 && h.m().pad == 0 && h.m().velocity == 127);

  std::uint8_t fx = 0xEE;
  h.set(1, I(2), &fx);
  CHECK(fx == EffectRepaint);
  h.set(2, B(false), &fx);
  CHECK(fx == EffectRepaint);
  h.set(3, B(true), &fx);
  CHECK(fx == EffectNone);
  h.set(6, E(1), &fx);
  CHECK(fx == EffectNone);
  h.set(9, E(1), &fx);
  CHECK(fx == EffectNone);
  h.set(11, I(3), &fx);
  CHECK(fx == (EffectReleaseNotes | EffectRepaint));
}

void attribute_round_trip() {
  Harness a;
  a.select_pad(0);
  a.set(1, I(-7));
  a.set(2, B(true));
  a.select_pad(2);
  a.set(3, B(true));
  a.select_pad(3);
  a.set(4, B(true));
  a.set(5, I(3));
  a.select_pad(4);
  a.set(6, E(2));
  a.select_pad(5);
  a.set(7, B(true));
  a.set(8, B(true));
  a.select_pad(0);

  Snapshot snap{};
  AttributeVisitor record{&snap, record_attr};
  a.d().attributes(a.state, record);
  CHECK(snap.attr_count == 8);

  Harness b;
  Snapshot restored = snap;
  restored.attr_count = 0;
  AttributeVisitor restore{&restored, restore_attr};
  b.d().attributes(b.state, restore);
  CHECK(restored.attr_count == 8);

  for (std::uint8_t pad = 0; pad < 12; ++pad) {
    a.press(key(pad));
    b.press(key(pad));
    CHECK(a.out.on.count == b.out.on.count);
    for (std::uint16_t i = 0; i < a.out.on.count && i < b.out.on.count; ++i) {
      CHECK(a.out.on.items[i].pitch == b.out.on.items[i].pitch);
    }
    a.release(key(pad));
    b.release(key(pad));
  }
  CHECK(b.m().notes[0] == -7 && b.m().inversions[3] == 3 && b.m().sus[4] == 2 && b.m().dim[5] == 1);

  // A fresh module still reports source defaults, so the restore test is not vacuous.
  Harness fresh;
  CHECK(fresh.m().notes[0] == 1 && fresh.m().dim[5] == 0);
}

void held_note_release() {
  Harness h;
  h.press(key(0));
  { const std::uint8_t e[] = {60, 64, 67}; CHECK(same(h.out.on, e)); }
  CHECK(h.out.off.count == 0);

  // Pad 1 shares pitch 60: retrigger emits an off before the new on.
  h.press(key(1));
  { const std::uint8_t off[] = {60}; const std::uint8_t on[] = {60}; CHECK(same(h.out.off, off)); CHECK(same(h.out.on, on)); }

  // Releasing pad 0 must not silence 60, which pad 1 still holds.
  h.release(key(0));
  { const std::uint8_t off[] = {64, 67}; CHECK(same(h.out.off, off)); CHECK(h.out.on.count == 0); }

  h.release(key(1));
  { const std::uint8_t off[] = {60}; CHECK(same(h.out.off, off)); }
  CHECK(!h.any_last_on());

  // Source multiplicity: two entries for the same pad release as two offs.
  h.press(key(1));
  h.press(key(1));
  h.release(key(1));
  { const std::uint8_t off[] = {60, 60}; CHECK(same(h.out.off, off)); }
  CHECK(!h.any_last_on());

  // Releasing an unpressed pad produces nothing.
  h.release(key(7));
  CHECK(h.out.off.count == 0 && h.out.on.count == 0);

  // Stop clears the relationship table, so later releases are no-ops.
  h.press(key(0));
  h.d().reset(h.state, ResetReason::Stop);
  h.release(key(0));
  CHECK(h.out.off.count == 0);
  CHECK(!matrix_release_pending(h.state));

  // Parameter changes do not clear held relationships.
  h.press(key(0));
  h.d().reset(h.state, ResetReason::ParameterChange);
  h.release(key(0));
  { const std::uint8_t off[] = {60, 64, 67}; CHECK(same(h.out.off, off)); }
  CHECK(!h.any_last_on());
}

void pressure_on_polyphony_is_bounded_and_balanced() {
  Harness h;
  // Pads 0, 3 and 6 with sevenths and ninths give 5+5+5 notes; the batch holds 12.
  for (std::uint8_t pad : std::initializer_list<std::uint8_t>{0, 3, 6}) {
    h.select_pad(pad);
    h.set(2, B(true));
    h.set(3, B(true));
    h.set(4, B(true));
  }
  h.step(frame({{key(0), true, 0}, {key(3), true, 0}, {key(6), true, 0}}));
  CHECK(h.out.on.count == kPolyphony);
  CHECK(h.m().output_overflow >= 1);
  CHECK(h.m().relationships.count == 12);  // Only notes actually sounded are tracked.

  // Source has_out_note: an off is suppressed while another pad still sounds the pitch.
  // Pad 0 releases 60 only (64/67/71/74 are held by pad 3); pad 3 releases 64, 74, 77 (67/71 held by pad 6);
  // pad 6 releases 67 and 71.
  h.step(frame({{key(0), false, 0}, {key(3), false, 0}, {key(6), false, 0}}));
  { const std::uint8_t off[] = {60, 64, 74, 77, 67, 71}; CHECK(same(h.out.off, off)); }
  CHECK(h.m().relationships.count == 0);
  CHECK(!matrix_release_pending(h.state));
  CHECK(!h.any_last_on());
}

void pressure_off_overflow_defers_without_loss() {
  Harness h;
  h.select_pad(0);
  h.set(3, B(true));
  h.set(4, B(true));
  // Three presses of pad 0 leave 15 relationship entries across 5 pitches.
  h.press(key(0));
  h.press(key(0));
  h.press(key(0));
  CHECK(h.m().relationships.count == 15);

  // A single release owes 15 offs; the batch holds 12, so 3 stay pending.
  h.release(key(0));
  CHECK(h.out.off.count == 12);
  CHECK(matrix_release_pending(h.state));
  CHECK(h.m().relationships.count == 3);
  CHECK(h.m().output_overflow >= 3);

  // The next process call drains the owed offs without any new key events.
  h.step(InputFrame{});
  CHECK(h.out.off.count == 3);
  CHECK(!matrix_release_pending(h.state));
  CHECK(h.m().relationships.count == 0);
  CHECK(!h.any_last_on());
}

void pressure_relationship_capacity() {
  Harness h;
  for (std::uint8_t pad = 0; pad < 8; ++pad) {
    h.select_pad(pad);
    h.set(2, B(true));
    h.set(3, B(true));
    h.set(4, B(true));
  }
  // Seven five-note chords fill 35 of 36 relationship slots.
  for (std::uint8_t pad = 0; pad < 7; ++pad) h.press(key(pad));
  CHECK(h.m().relationships.count == 35);

  // Pad 7 gets one slot: its first note sounds; the second is dropped, not left held.
  h.press(key(7));
  CHECK(h.m().relationships.count == MatrixRelationships::kCapacity);
  CHECK(h.out.on.count == 1);
  CHECK(h.out.on.items[0].pitch == 67);
  CHECK(h.m().relationship_overflow == 1);

  for (std::uint8_t pad = 0; pad < 8; ++pad) h.release(key(pad));
  CHECK(h.m().relationships.count == 0);
  CHECK(!h.any_last_on());
}

void init_reset_clears_state() {
  Harness h;
  h.press(key(0));
  h.d().reset(h.state, ResetReason::Init);
  CHECK(h.m().relationships.count == 0 && h.m().temp_count == 0);
}
}  // namespace

int main() {
  schema_and_registry();
  defaults_and_pad_releases();
  chord_modifiers_on_pad0();
  bass_spread_and_order();
  velocity_and_global_mapping();
  follow_played_pad();
  reset_layouts();
  parameter_validation_and_effects();
  attribute_round_trip();
  held_note_release();
  pressure_on_polyphony_is_bounded_and_balanced();
  pressure_off_overflow_defers_without_loss();
  pressure_relationship_capacity();
  init_reset_clears_state();
  if (g_failures != 0) {
    std::fprintf(stderr, "t08c_matrix: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("t08c_matrix: all checks passed");
  return 0;
}
