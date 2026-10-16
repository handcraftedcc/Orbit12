#include <cstdint>
#include <cstdio>
#include <limits>

#include "domain/time.hpp"
#include "domain/types.hpp"
#include "fakes.hpp"

namespace {

int failures = 0;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #expression);                                                \
      ++failures;                                                                \
    }                                                                            \
  } while (false)

bool same_pitch_channel(const orbit::Note& lhs, const orbit::Note& rhs) {
  return lhs.pitch == rhs.pitch && lhs.channel == rhs.channel;
}

void fixed_buffer_contract() {
  orbit::NoteBuffer notes;
  const orbit::Note c4{60, 100, 0};
  CHECK(orbit::append_note(notes, c4) == orbit::PushResult::Added);
  CHECK(orbit::append_note(notes, c4) == orbit::PushResult::Added);
  CHECK(notes.count == 2);  // append intentionally preserves multiplicity
  CHECK(orbit::insert_unique_note(notes, c4, same_pitch_channel) ==
        orbit::PushResult::Duplicate);
  CHECK(orbit::insert_unique_note(notes, orbit::Note{60, 100, 1}, same_pitch_channel) ==
        orbit::PushResult::Added);
  CHECK(orbit::append_note(notes, orbit::Note{128, 1, 0}) == orbit::PushResult::Invalid);
  CHECK(orbit::append_note(notes, orbit::Note{60, 128, 0}) == orbit::PushResult::Invalid);
  CHECK(orbit::append_note(notes, orbit::Note{60, 100, 16}) == orbit::PushResult::Invalid);
  for (std::uint8_t pitch = 61; notes.count < notes.capacity(); ++pitch) {
    CHECK(orbit::append_note(notes, orbit::Note{pitch, 1, 0}) == orbit::PushResult::Added);
  }
  const auto count_before_full = notes.count;
  const auto first_before_full = notes.items[0];
  CHECK(orbit::append_note(notes, orbit::Note{90, 1, 0}) == orbit::PushResult::Full);
  CHECK(notes.count == count_before_full);
  CHECK(notes.items[0].pitch == first_before_full.pitch &&
        notes.items[0].velocity == first_before_full.velocity &&
        notes.items[0].channel == first_before_full.channel);
  notes.clear();
  CHECK(notes.empty());
}

void defaults_and_setters() {
  const orbit::GlobalState defaults{};
  CHECK(defaults.valid());
  CHECK(defaults.key == 0 && defaults.scale == 1 && defaults.octave == 3);
  CHECK(defaults.bpm == 120 && defaults.swing == 0.0f);
  CHECK(defaults.transport_mode == orbit::TransportMode::Internal);
  CHECK(defaults.timing_step == orbit::TimingStep::Sixteenth);
  orbit::GlobalState state;
  CHECK(!state.set_key(12));
  CHECK(state.key == 0);
  CHECK(state.set_scale(20));
  CHECK(!state.set_scale(21));
  CHECK(state.set_bpm(1) && state.set_bpm(300));
  CHECK(!state.set_bpm(0) && !state.set_bpm(301));
  CHECK(state.set_swing(1.0f));
  CHECK(!state.set_swing(-0.01f) && !state.set_swing(1.01f));
  CHECK(!state.set_swing(std::numeric_limits<float>::quiet_NaN()));
  state.swing = std::numeric_limits<float>::quiet_NaN();
  CHECK(!state.valid());
  state.swing = 0.0f;
  state.octave = -200000;
  state.key_offset = -400000;
  CHECK(state.valid());  // Source state is unrestricted Python int.
  CHECK(state.set_transport_mode(1));
  CHECK(!state.set_transport_mode(2));
}

void time_contract() {
  CHECK(orbit::elapsed(2U, 0xfffffffeU) == 4U);
  CHECK(orbit::deadline_reached(10U, 10U));
  CHECK(orbit::deadline_reached(2U, 0xfffffffeU));
  CHECK(!orbit::deadline_reached(0xfffffffeU, 2U));
  CHECK(orbit::valid_deadline_distance(0xfffffffeU, 2U));
  CHECK(!orbit::valid_deadline_distance(2U, 0xfffffffeU));
}

void deterministic_fakes() {
  FakeClock clock;
  CHECK(clock.push(orbit::TimeSample{0xfffffff0U, 0xfffffff0U}));
  CHECK(clock.push(orbit::TimeSample{2U, 4U}));
  CHECK(clock.sample().ms == 0xfffffff0U);
  CHECK(clock.sample().us == 4U);
  CHECK(clock.sample().ms == 2U);  // Exhaustion holds the final sample.

  FakeRandom random;
  CHECK(random.push(7U) && random.push(11U) && random.push(19U));
  std::uint32_t draw = 0;
  const auto source = random.source();
  CHECK(source.draw(draw) && draw == 7U);
  CHECK(source.draw(draw) && draw == 11U);
  CHECK(source.draw(draw) && draw == 19U);
  CHECK(source.draw(draw) && draw == 19U);
  CHECK(!orbit::RandomSource{}.draw(draw));

  RefusingMidi midi;
  CHECK(!midi.try_send(std::uint8_t{0}));
  CHECK(midi.attempts == 1U);

  RefusingStorage storage;
  std::uint8_t byte = 0;
  CHECK(!storage.read("Scene01.txt", &byte, 1));
  CHECK(!storage.write("Scene01.txt", &byte, 1));
  CHECK(!storage.rename("Scene01.tmp", "Scene01.txt"));
  CHECK(storage.calls == 3U);
}

}  // namespace

int main() {
  fixed_buffer_contract();
  defaults_and_setters();
  time_contract();
  deterministic_fakes();
  if (failures != 0) {
    std::fprintf(stderr, "%d native assertion(s) failed\n", failures);
    return 1;
  }
  std::puts("T01 native foundation checks passed");
  return 0;
}
