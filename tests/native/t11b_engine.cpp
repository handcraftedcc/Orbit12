// T11b engine behavior: release safety through the bounded bridge, stop/panic
// release requirements, construction side effects, recovery and external clock.
// Expected pitches are source-derived: input id 9 in C major, octave 3 maps to
// MIDI 60 (behavior-contracts.md, note input golden case).
#include <cstring>
#include <memory>

#include "t11b_support.hpp"

int g_failures = 0;
using namespace t11b;

namespace {

constexpr std::uint8_t kPad60 = 9;  // physical key index whose input id is 9 -> MIDI 60

struct BridgeRig : Rig {
  std::uint16_t mask = 0;
  void pub(InputKind kind, std::uint8_t key, std::int16_t value) {
    PhysicalEvent e{};
    e.kind = kind; e.key = key; e.value = value; e.at_ms = ms;
    if (kind == InputKind::KeyEdge) {
      if (value) mask = static_cast<std::uint16_t>(mask | (1U << key));
      else mask = static_cast<std::uint16_t>(mask & ~(1U << key));
    }
    (void)bridge.publish(e, mask);
  }
  bool pub_checked(InputKind kind, std::uint8_t key, std::int16_t value) {
    PhysicalEvent e{};
    e.kind = kind; e.key = key; e.value = value; e.at_ms = ms;
    if (kind == InputKind::KeyEdge) {
      if (value) mask = static_cast<std::uint16_t>(mask | (1U << key));
      else mask = static_cast<std::uint16_t>(mask & ~(1U << key));
    }
    return bridge.publish(e, mask);
  }
  void poll(std::uint32_t dt = 1) { ms += dt; (void)c.poll(bridge, args()); }
};

void select_input(Rig& r, std::uint8_t registry_index) {
  // Encoder held + KEY4 opens the selector on the input slot; turn; KEY9 applies.
  r.enc_down(); r.step();
  r.chord_key(3);           // KEY4
  r.enc_up(); r.step();
  r.turn(registry_index); r.step();
  r.enc_down(); r.step();
  r.chord_key(8);           // KEY9
  r.enc_up(); r.step();
}

void play_and_release() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60);
  r->step();
  CHECK(r->c.transport().running());                       // first note starts the internal clock
  CHECK(r->cap.count(MidiKind::Start) == 1);
  CHECK(r->cap.count(MidiKind::NoteOn, 60) == 1);
  CHECK(r->c.output().is_sounding(0, 60));
  r->key_up(kPad60);
  r->step();
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->c.output().held_count() == 0);
}

void consumed_encoder_release_all_notes_off() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60);
  r->step();
  r->key_down(8);          // second note (id 0): stays held
  r->step();
  const auto calls = r->c.diagnostics().release_calls;
  r->enc_down(); r->step();
  r->key_up(kPad60); r->step();         // release while the encoder is held: marks consumed
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  r->enc_up(); r->step();
  CHECK(r->c.diagnostics().release_calls == calls + 1);   // all_notes_off, nothing else
  CHECK(r->c.section() == UiSection::Chain);              // knob action was consumed
  CHECK(r->c.output().held_count() == 0);                 // the still-held key's note was released too
}

void shortcut_press_does_not_sound_or_enter() {
  auto r = std::make_unique<Rig>();
  r->enc_down(); r->step();
  r->key_down(kPad60); r->step();       // KEY10 with the encoder held is pad offset -1
  r->key_up(kPad60); r->step();
  CHECK(r->cap.count(MidiKind::NoteOn) == 0);
  CHECK(r->c.global().key_offset == -1);
  r->enc_up(); r->step();
  CHECK(r->c.section() == UiSection::Chain);
}

void stop_and_start_shortcuts() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60); r->step();
  CHECK(r->c.output().is_sounding(0, 60));
  r->cap.clear();
  r->enc_down(); r->step();
  r->key_down(0); r->step();            // KEY1 stop
  CHECK(!r->c.transport().running());
  CHECK(r->cap.count(MidiKind::Stop) == 1);
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);   // stop_all_modules -> all_notes_off
  CHECK(!r->c.output().is_sounding(0, 60));
  r->key_up(0); r->step();
  r->key_down(1); r->step();            // KEY2 start
  CHECK(r->c.transport().running());
  CHECK(r->cap.count(MidiKind::Start) == 1);
  r->key_up(1); r->enc_up(); r->step();
}

void bridge_flood_keeps_release() {
  auto r = std::make_unique<BridgeRig>();
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->poll();
  CHECK(r->cap.count(MidiKind::NoteOn, 60) == 1);
  for (int i = 0; i < 63; ++i) CHECK(r->pub_checked(InputKind::EncoderDelta, 0, 1));
  CHECK(r->pub_checked(InputKind::KeyEdge, kPad60, 0));      // 64th record: the release
  r->poll();
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->c.output().held_count() == 0);
  CHECK(!r->bridge.recovery_required());
}

void bridge_overflow_recovers_without_stuck_notes() {
  auto r = std::make_unique<BridgeRig>();
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->poll();
  CHECK(r->c.output().is_sounding(0, 60));
  for (int i = 0; i < 64; ++i) CHECK(r->pub_checked(InputKind::EncoderDelta, 0, 1));
  CHECK(!r->pub_checked(InputKind::KeyEdge, kPad60, 0));      // the release is lost by the queue ...
  r->poll();                                                  // ... but the fault latch recovers it
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->c.output().held_count() == 0);
  CHECK(r->c.diagnostics().emergency_releases >= 1);
  // Presses stay gated until an authoritative all-keys-up resync arrives.
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->pub(InputKind::KeyEdge, kPad60, 0);
  r->poll();
  CHECK(r->cap.count(MidiKind::NoteOn) == 1);                 // still only the first note
  r->pub(InputKind::Resync, 0, 0);
  r->poll();
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->poll();
  CHECK(r->cap.count(MidiKind::NoteOn, 60) == 2);
  r->pub(InputKind::KeyEdge, kPad60, 0);
  r->poll();
  CHECK(r->c.output().held_count() == 0);
}

void sequence_gap_releases() {
  auto r = std::make_unique<BridgeRig>();
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->poll();
  CHECK(r->c.output().is_sounding(0, 60));
  r->pub(InputKind::EncoderDelta, 0, 1);    // sequence 1: stolen below
  r->pub(InputKind::KeyEdge, kPad60, 0);    // sequence 2: arrives with a gap
  PhysicalEvent lost{};
  CHECK(r->bridge.try_pop(lost));
  r->poll();
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->c.output().held_count() == 0);
  CHECK(r->bridge.awaiting_resync());
}

void resync_mask_synthesizes_release() {
  auto r = std::make_unique<BridgeRig>();
  r->pub(InputKind::KeyEdge, kPad60, 1);
  r->poll();
  CHECK(r->c.held_keys() == (1U << kPad60));
  // The release was never published, but an authoritative mask says the key is up.
  PhysicalEvent e{};
  e.kind = InputKind::Resync; e.at_ms = r->ms;
  (void)r->bridge.publish(e, 0);
  r->poll();
  CHECK(r->c.held_keys() == 0);
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->c.diagnostics().resync_releases == 1);
}

void matrix_release_drains() {
  auto r = std::make_unique<Rig>();
  select_input(*r, 3);                               // matrix (source registry order)
  CHECK(std::strcmp(r->c.slot_module_id(kSlotIn), "matrix") == 0);
  // Every pad a 7th+9th chord (five notes), pressed over several passes, so more than
  // twelve notes sound and one release pass cannot emit all offs.
  auto& m = r->c.input_state().storage.matrix;
  for (std::uint8_t p = 0; p < 12; ++p) { m.chords[p] = 1; m.sevenths[p] = 1; m.ninths[p] = 1; }
  for (std::uint8_t k = 0; k < 12; ++k) { r->key_down(k); r->step(); }
  const auto sounding = r->c.output().held_count();
  CHECK(sounding > 12);
  for (std::uint8_t k = 0; k < 12; ++k) r->key_up(k);
  r->step();
  CHECK(matrix_release_pending(r->c.input_state()));  // off batch (12) was full: releases are owed
  for (int i = 0; i < 12; ++i) r->step();            // owed offs drain on later passes
  CHECK(!matrix_release_pending(r->c.input_state()));
  CHECK(r->c.output().held_count() == 0);
  CHECK(r->c.output().owed_count() == 0);
}

void chord_walk_offset_applied_on_construction() {
  auto r = std::make_unique<Rig>();
  select_input(*r, 5);                               // chord_walk
  CHECK(std::strcmp(r->c.slot_module_id(kSlotIn), "chord_walk") == 0);
  CHECK(r->c.global().key_offset == -2);
  const ParamRow* ofs = row(r->c, kSlotIn, "key_offset");
  CHECK(ofs != nullptr && ofs->value_i == -2);       // PADOFS row is built after the request
}

void construction_seeds_running_flag() {
  auto r = std::make_unique<Rig>();
  r->enc_down(); r->step(); r->key_down(1); r->step(); r->key_up(1); r->enc_up(); r->step();  // KEY2 start
  CHECK(r->c.transport().running());
  r->turn(1); r->step();                              // slot 1
  r->enc_down(); r->step(); r->chord_key(3); r->enc_up(); r->step();
  r->turn(2); r->step();                              // arp_walk
  r->enc_down(); r->step(); r->chord_key(8); r->enc_up(); r->step();
  CHECK(std::strcmp(r->c.slot_module_id(kSlotP1), "arp_walk") == 0);
  CHECK(r->c.chain().state(0)->storage.arp_walk.was_running);
  // A module created while stopped keeps the stopped default.
  auto s = std::make_unique<Rig>();
  s->turn(1); s->step();
  s->enc_down(); s->step(); s->chord_key(3); s->enc_up(); s->step();
  s->turn(2); s->step();
  s->enc_down(); s->step(); s->chord_key(8); s->enc_up(); s->step();
  CHECK(std::strcmp(s->c.slot_module_id(kSlotP1), "arp_walk") == 0);
  CHECK(!s->c.chain().state(0)->storage.arp_walk.was_running);
}

void module_replacement_releases() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60); r->step();
  CHECK(r->c.output().is_sounding(0, 60));
  const auto before = r->c.diagnostics().deviation_releases;
  r->enc_down(); r->step(); r->chord_key(3); r->enc_up(); r->step();   // KEY4 selector on the input slot
  r->turn(1); r->step();
  r->enc_down(); r->step(); r->chord_key(8); r->enc_up(); r->step();   // apply: new input module
  CHECK(r->c.diagnostics().deviation_releases == before + 1);
  CHECK(r->cap.count(MidiKind::NoteOff, 60) >= 1);
  CHECK(r->c.output().held_count() == 0);
}

void usb_disconnect_and_reconnect() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60); r->step();
  CHECK(r->c.output().is_sounding(0, 60));
  r->cap.clear();
  PollArgs a = r->args();
  a.midi_connected = false;
  r->ms += 1; a.now = r->time();
  (void)r->c.step(a);
  r->key_up(kPad60);
  r->ms += 1; a.now = r->time();
  (void)r->c.step(a);
  CHECK(r->cap.packets.empty());                        // nothing is written while disconnected
  for (int i = 0; i < 4; ++i) r->step();                // reconnect
  CHECK(r->cap.count(MidiKind::NoteOff, 60) >= 1);      // the release was never lost
  CHECK(r->c.output().held_count() == 0);
}

void clock_backlog_recovery_stops_transport() {
  auto r = std::make_unique<Rig>();
  r->key_down(kPad60); r->step();
  CHECK(r->c.transport().running());
  r->cap.accept = false;
  for (int i = 0; i < 400 && !r->c.output().recovering(); ++i) r->step(20);
  CHECK(r->c.output().recovering());
  r->step(1);
  CHECK(!r->c.transport().running());                   // generation stops until the user restarts
  r->cap.accept = true;
  for (int i = 0; i < 20; ++i) r->step(1);
  CHECK(r->cap.count(MidiKind::NoteOff, 60) >= 1);
  CHECK(r->cap.count(MidiKind::Stop) >= 1);
  CHECK(!r->c.output().recovering());
  CHECK(r->c.output().held_count() == 0);
}

void external_stop_releases_notes() {
  auto r = std::make_unique<Rig>();
  // SET -> TRMDE -> EXT through the UI, as a player would.
  r->turn(-1); r->step();
  r->enc_down(); r->enc_up(); r->step();               // parameter selection on SET
  r->turn(4); r->step();                               // TRMDE
  r->enc_down(); r->enc_up(); r->step();               // edit
  r->turn(1); r->step();
  r->enc_down(); r->enc_up(); r->step();               // exit: mode applied
  CHECK(r->c.global().transport_mode == TransportMode::External);
  // Back to the input slot and enter chain view.
  r->enc_down(); r->step(); r->chord_key(4); r->enc_up(); r->step();   // KEY5 chain view
  r->turn(1); r->step();
  CHECK(r->c.active_chain() == kSlotIn);
  MidiInput clock{MidiInputKind::Clock};
  PollArgs a = r->args();
  a.midi_in = &clock; a.midi_in_count = 1;
  r->ms += 1; a.now = r->time();
  (void)r->c.step(a);
  CHECK(r->c.transport().running());                    // external clock starts the transport
  r->key_down(kPad60); r->step();
  CHECK(r->cap.count(MidiKind::NoteOn, 60) == 1);       // no auto-start in external mode, notes still play
  MidiInput stop{MidiInputKind::Stop};
  a = r->args(); a.midi_in = &stop; a.midi_in_count = 1;
  r->ms += 1; a.now = r->time();
  (void)r->c.step(a);
  CHECK(!r->c.transport().running());
  r->step();
  CHECK(r->cap.count(MidiKind::NoteOff, 60) == 1);      // clock_stop(send_out=False) still stops modules
}

void snapshot_publication() {
  auto r = std::make_unique<Rig>();
  UiSnapshotQueue<UiModel> q;
  CHECK(r->c.publish_ui(q, 3, 100));                    // first publication
  CHECK(!r->c.publish_ui(q, 3, 120));                   // unchanged
  r->turn(1); r->step();
  CHECK(!r->c.publish_ui(q, 3, 110));                   // rate limit: <= 30 Hz
  CHECK(r->c.publish_ui(q, 3, 140));
  UiSnapshot<UiModel> newest{};
  CHECK(q.drain_newest(newest) == 2);
  CHECK(newest.epoch == 3 && newest.revision == 2);
  CHECK(newest.model.active_chain == kSlotP1);
  // Full queue: skip and retry later with the newest state.
  for (int i = 0; i < 3; ++i) { r->turn(1); r->step(); CHECK(r->c.publish_ui(q, 3, 200 + 40 * i)); }
  r->turn(1); r->step();
  CHECK(!r->c.publish_ui(q, 3, 400));
  CHECK(q.drain_newest(newest) == 3);
  CHECK(r->c.publish_ui(q, 3, 440));
}

}  // namespace

int main() {
  play_and_release();
  consumed_encoder_release_all_notes_off();
  shortcut_press_does_not_sound_or_enter();
  stop_and_start_shortcuts();
  bridge_flood_keeps_release();
  bridge_overflow_recovers_without_stuck_notes();
  sequence_gap_releases();
  resync_mask_synthesizes_release();
  matrix_release_drains();
  chord_walk_offset_applied_on_construction();
  construction_seeds_running_flag();
  module_replacement_releases();
  usb_disconnect_and_reconnect();
  clock_backlog_recovery_stops_transport();
  external_stop_releases_notes();
  snapshot_publication();
  if (g_failures == 0) std::printf("t11b_engine: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
