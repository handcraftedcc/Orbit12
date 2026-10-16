// Source oracle: circuitpython-source/src/apps/Orion/inputmodules/note_walk.py and chord_walk.py.
// Expected values are derived by hand from the Python algorithms with GlobalState
// defaults (key C, Major scale, octave 3). ChordWalk's __init__ forces key_offset to -2;
// the controller applies that request, which these fixtures model explicitly.
#include "domain/modules/input_registry.hpp"
#include "domain/modules/walk_inputs.hpp"
#include "domain/music.hpp"
#include <cstdint>
#include <cstdio>
#include <initializer_list>

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

// Physical key for logical pad p (source PADMAP).
std::uint8_t key(std::uint8_t pad) { return music::kPadMap[pad]; }

InputFrame frame(std::initializer_list<KeyEvent> events) {
  InputFrame f{};
  for (const auto& e : events) CHECK(f.keys.append(e) == PushResult::Added);
  return f;
}
KeyEvent down(std::uint8_t pad) { return KeyEvent{key(pad), true, 0}; }
KeyEvent up(std::uint8_t pad) { return KeyEvent{key(pad), false, 0}; }

// Same shape as the matrix fixture: the harness owns every object the context references.
struct Harness {
  GlobalState global{};
  Rng rng{};
  RandomSource random{&rng, draw};
  InputState state{};
  InputView view{};
  NoteBatch out{};
  const InputDescriptor* descriptor = nullptr;

  explicit Harness(const InputDescriptor& d) : descriptor(&d) { d.construct(state); }
  ProcessContext ctx() { return ProcessContext{{0, 0}, global, {}, random}; }
  void run(const InputFrame& f) { descriptor->process(state, ctx(), f, out, view); }
  bool set(std::uint8_t index, ParamValue v, std::uint8_t& effects) {
    return descriptor->set_param(state, index, v, global, effects);
  }
};

bool has_on(const NoteBuffer& b, std::uint8_t pitch, std::uint8_t velocity) {
  for (std::uint16_t i = 0; i < b.count; ++i) {
    if (b.items[i].pitch == pitch && b.items[i].velocity == velocity) return true;
  }
  return false;
}
bool has_off(const NoteBuffer& b, std::uint8_t pitch) {
  for (std::uint16_t i = 0; i < b.count; ++i) {
    if (b.items[i].pitch == pitch) return true;
  }
  return false;
}

// ---- note_walk ----

void note_walk_trace_and_release() {
  Harness h(note_walk_input_descriptor());
  // Press pad 0: root = convert_note(pad 0, octave 3) = C5 = 60; step 0.
  h.run(frame({down(0)}));
  CHECK(h.out.on.count == 1 && has_on(h.out.on, 60, 127));
  CHECK(h.out.off.count == 0);
  // Press pad 2 while pad 0 is held: step += 2 -> scale degree 2 = E5 = 64, retrigger 60 -> 64.
  h.run(frame({down(2)}));
  CHECK(h.out.off.count == 1 && has_off(h.out.off, 60));
  CHECK(h.out.on.count == 1 && has_on(h.out.on, 64, 127));
  // Releasing pad 2 while pad 0 is still held changes nothing audible.
  h.run(frame({up(2)}));
  CHECK(h.out.off.count == 0 && h.out.on.count == 0);
  // Releasing the last pad releases the sounding note.
  h.run(frame({up(0)}));
  CHECK(h.out.off.count == 1 && has_off(h.out.off, 64));
  CHECK(h.out.on.count == 0);
  // A later empty frame emits nothing.
  h.run(frame({}));
  CHECK(h.out.off.count == 0 && h.out.on.count == 0);
}

void note_walk_pad_attributes_and_controls() {
  Harness h(note_walk_input_descriptor());
  // Pad 3 (scale degree 3 = F) with octave 3 is F5 = 65.
  h.run(frame({down(3)}));
  CHECK(h.out.on.count == 1 && has_on(h.out.on, 65, 127));
  h.run(frame({up(3)}));

  // Velocity control applies to subsequent presses.
  std::uint8_t fx = 0xFF;
  CHECK(h.set(2, I(90), fx) && fx == EffectNone);
  h.run(frame({down(0)}));
  CHECK(has_on(h.out.on, 60, 90));
  h.run(frame({up(0)}));

  // Octave changes release notes and repaint; key offset is a global binding.
  CHECK(h.set(0, I(4), fx) && fx == (EffectReleaseNotes | EffectRepaint));
  CHECK(h.set(1, I(-2), fx) && fx == (EffectReleaseNotes | EffectRepaint));
  CHECK(!h.set(2, I(128), fx));
  CHECK(!h.set(2, E(1), fx));
  CHECK(note_walk_input_descriptor().param_count == 3);
  CHECK(note_walk_input_descriptor().params[0].global_binding == GlobalBinding::Octave);
  CHECK(note_walk_input_descriptor().params[1].global_binding == GlobalBinding::KeyOffset);
}

void note_walk_stop_and_no_attributes() {
  Harness h(note_walk_input_descriptor());
  h.run(frame({down(0)}));
  // Source stop() clears walk state without emitting an off; the output layer's
  // all-notes-off covers the sounding note.
  h.descriptor->reset(h.state, ResetReason::Stop);
  h.run(frame({up(0)}));
  CHECK(h.out.off.count == 0 && h.out.on.count == 0);
  // After reset a new press starts a fresh walk at the pressed pad.
  h.run(frame({down(3)}));
  CHECK(has_on(h.out.on, 65, 127));
  // note_walk saves no attributes.
  struct Counter { std::uint8_t n = 0; } counter;
  AttributeVisitor v{&counter, [](void* ctx, AttributeDescriptor&) noexcept {
    ++static_cast<Counter*>(ctx)->n;
    return true;
  }};
  h.descriptor->attributes(h.state, v);
  CHECK(counter.n == 0);
}

// ---- chord_walk ----

void chord_walk_trace_default() {
  Harness h(chord_walk_input_descriptor());
  // Controller applies the ChordWalk.__init__ key_offset = -2 request.
  std::int32_t offset = 0;
  CHECK(chord_walk_take_key_offset_request(h.state, offset) && offset == -2);
  CHECK(!chord_walk_take_key_offset_request(h.state, offset));
  h.global.key_offset = -2;

  // Trigger pad 1 (index 0) with step = 0 + (-2) = -2: register [60,64,67] at -2 -> 52.
  h.run(frame({down(1)}));
  CHECK(h.out.on.count == 1 && has_on(h.out.on, 52, 127));
  // Trigger pad 2 (index 1) steps +1 to -1 -> 55, retriggering 52 -> 55.
  h.run(frame({down(2)}));
  CHECK(h.out.off.count == 1 && has_off(h.out.off, 52));
  CHECK(h.out.on.count == 1 && has_on(h.out.on, 55, 127));
  // Releasing trigger 1 keeps trigger 2 sounding; trigger_root moves to it.
  h.run(frame({up(1)}));
  CHECK(h.out.off.count == 0 && h.out.on.count == 0);
  h.run(frame({up(2)}));
  CHECK(h.out.off.count == 1 && has_off(h.out.off, 55));
  CHECK(h.out.on.count == 0);
}

void chord_walk_preview_and_chord_pads() {
  Harness h(chord_walk_input_descriptor());
  h.global.key_offset = -2;
  std::uint8_t fx = 0;
  CHECK(h.set(5, B(true), fx) && fx == EffectNone);

  // Chord pad 0 (slot 0, default register C-E-G) previews its notes at VEL 127.
  h.run(frame({down(0)}));
  CHECK(h.out.on.count == 3 && has_on(h.out.on, 60, 127) && has_on(h.out.on, 64, 127) && has_on(h.out.on, 67, 127));
  CHECK(h.out.off.count == 0);
  // Releasing the chord pad previews the same three note-offs.
  h.run(frame({up(0)}));
  CHECK(h.out.off.count == 3 && has_off(h.out.off, 60) && has_off(h.out.off, 64) && has_off(h.out.off, 67));
  CHECK(h.out.on.count == 0);

  // Editing pad 0 (note 3 -> degrees 2,4,6 = E5,G5,B5) rebuilds the held chord's register.
  CHECK(h.set(0, I(0), fx));
  CHECK(h.set(1, I(3), fx) && fx == EffectRepaint);
  h.run(frame({down(0)}));
  CHECK(h.out.on.count == 3 && has_on(h.out.on, 64, 127) && has_on(h.out.on, 67, 127) && has_on(h.out.on, 71, 127));
  h.run(frame({up(0)}));

  // Sus and power change the register as the source build_register does.
  CHECK(h.set(3, E(2), fx));
  CHECK(h.set(4, B(true), fx));
  CHECK(h.set(2, B(true), fx));
  h.run(frame({down(0)}));
  // sus (root+3), fifth and seventh (root+6) give a four-note register.
  // and fifth stay present with the seventh 6 degrees above root.
  CHECK(h.out.on.count == 4);
  h.run(frame({up(0)}));
}

void chord_walk_controls_and_overflow_release() {
  Harness h(chord_walk_input_descriptor());
  h.global.key_offset = -2;
  std::uint8_t fx = 0;
  CHECK(!h.set(0, I(4), fx));          // pad range 0..3
  CHECK(!h.set(1, I(25), fx));         // note range -24..24
  CHECK(!h.set(3, E(3), fx));          // sus range 0..2
  CHECK(h.set(6, I(2), fx) && fx == (EffectReleaseNotes | EffectRepaint));
  CHECK(h.set(7, I(0), fx) && fx == (EffectReleaseNotes | EffectRepaint));
  CHECK(h.set(8, I(127), fx));
  CHECK(chord_walk_input_descriptor().param_count == 9);
  CHECK(chord_walk_input_descriptor().params[6].global_binding == GlobalBinding::Octave);
  CHECK(chord_walk_input_descriptor().params[7].global_binding == GlobalBinding::KeyOffset);

  // Preview releases that exceed the 12-entry off batch are owed, not dropped:
  // five releases of chord pad 0 (3 notes each) emit 12 offs and queue 3 for the next call.
  h.set(5, B(true), fx);
  h.run(frame({down(0)}));
  h.run(frame({up(0), up(0), up(0), up(0), up(0)}));
  CHECK(h.out.off.count == 12);
  h.run(frame({}));
  CHECK(h.out.off.count == 3);
  h.run(frame({}));
  CHECK(h.out.off.count == 0);
}

void chord_walk_stop_clears_state() {
  Harness h(chord_walk_input_descriptor());
  h.global.key_offset = -2;
  h.run(frame({down(1)}));
  CHECK(has_on(h.out.on, 52, 127));
  h.descriptor->reset(h.state, ResetReason::Stop);
  // Register is cleared: a trigger press sounds nothing until a chord is pressed.
  h.run(frame({up(1), down(2)}));
  CHECK(h.out.on.count == 0 && h.out.off.count == 0);
  // Pressing a chord rebuilds the register, then triggers sound again.
  h.run(frame({down(0)}));
  h.run(frame({down(2)}));
  CHECK(h.out.on.count == 1);
}

void chord_walk_attributes_and_registry() {
  InputState s{};
  construct_chord_walk_input(s);
  struct Seen { std::uint8_t n = 0; } seen;
  bool notes_ok = false;
  AttributeVisitor v{&seen, [](void* ctx, AttributeDescriptor& a) noexcept {
    ++static_cast<Seen*>(ctx)->n;
    return a.element_count == 4 && a.name != nullptr;
  }};
  chord_walk_input_descriptor().attributes(s, v);
  notes_ok = seen.n == 4;
  CHECK(notes_ok);
  CHECK(input_module_count() == 6);
  CHECK(find_input_module("note_walk") == &note_walk_input_descriptor());
  CHECK(find_input_module("chord_walk") == &chord_walk_input_descriptor());
  CHECK(input_module_at(4) == &note_walk_input_descriptor());
  CHECK(input_module_at(5) == &chord_walk_input_descriptor());
}
}  // namespace

int main() {
  note_walk_trace_and_release();
  note_walk_pad_attributes_and_controls();
  note_walk_stop_and_no_attributes();
  chord_walk_trace_default();
  chord_walk_preview_and_chord_pads();
  chord_walk_controls_and_overflow_release();
  chord_walk_stop_clears_state();
  chord_walk_attributes_and_registry();
  if (g_failures != 0) {
    std::fprintf(stderr, "t08d_walk: %d failure(s)\n", g_failures);
    return 1;
  }
  std::puts("t08d_walk: all checks passed");
  return 0;
}
