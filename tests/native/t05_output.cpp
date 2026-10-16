#include "domain/output.hpp"
#include <cassert>
#include <cstdio>

struct FakeMidi {
  orbit::MidiPacket packets[256]{};
  std::uint16_t count = 0;
  std::uint16_t refusals = 0;
  static bool send(void* context, const orbit::MidiPacket& packet) {
    auto& self = *static_cast<FakeMidi*>(context);
    if (self.refusals) { --self.refusals; return false; }
    self.packets[self.count++] = packet;
    return true;
  }
};
static orbit::NoteBatch one_on(std::uint8_t pitch, std::uint8_t velocity = 100) {
  orbit::NoteBatch b{}; b.on.append({pitch, velocity, 0}); return b;
}
static orbit::NoteBatch one_off(std::uint8_t pitch) {
  orbit::NoteBatch b{}; b.off.append({pitch, 0, 0}); return b;
}
int main() {
  using namespace orbit;
  FakeMidi fake{}; MidiPort port{&fake, &FakeMidi::send};

  // Source OutputManager deduplicates same pitch/channel, tracks channels as
  // part of identity, and emits offs before ons for same-tick retriggers.
  Output out;
  assert(out.submit(one_on(60), 0)); assert(out.submit(one_on(60), 1));
  assert(out.submit(one_on(60), 0));
  assert(out.flush(port) == 2 && out.held_count() == 2);
  assert(fake.packets[0].kind == MidiKind::NoteOn && fake.packets[0].channel == 0);
  assert(fake.packets[1].kind == MidiKind::NoteOn && fake.packets[1].channel == 1);
  NoteBatch retrigger{}; retrigger.off.append({60, 0, 0}); retrigger.on.append({60, 90, 0});
  assert(out.submit(retrigger, 0)); assert(out.flush(port) == 2);
  assert(fake.packets[2].kind == MidiKind::NoteOff && fake.packets[2].channel == 0);
  assert(fake.packets[3].kind == MidiKind::NoteOn && fake.packets[3].channel == 0);
  assert(out.held_count() == 2);

  // A refused write retains the exact packet and does not fabricate held state.
  Output blocked; fake.refusals = 1;
  assert(blocked.submit(one_on(64)));
  assert(blocked.flush(port) == 0 && blocked.pending() == 1 && blocked.held_count() == 0);
  assert(blocked.diagnostics().refused == 1);
  assert(blocked.flush(port) == 1 && blocked.held_count() == 1);

  // Capacity: the single FIFO holds 48 events. A press that does not fit is
  // refused, counted and never marked sounding (no recovery: nothing was
  // promised to the instrument). This replaces the old 24-identity expectation:
  // sounding identities are now a 2048-bit map; only the FIFO is bounded.
  Output full;
  for (std::uint8_t pitch = 0; pitch < 48; ++pitch) assert(full.submit(one_on(pitch)));
  assert(full.pending() == 48 && full.submit(one_on(60)) == false);
  assert(!full.recovering() && full.diagnostics().queue_overflow == 1 && full.pending() == 48);
  assert(full.flush(port) == 32 && full.flush(port) == 16 && full.held_count() == 48);
  assert(!full.is_sounding(0, 60));

  // Panic queues explicit offs; disconnected output clears stale queue and
  // performs cleanup after reconnect before allowing a user restart.
  Output panic; assert(panic.submit(one_on(70))); assert(panic.flush(port) == 1);
  assert(panic.all_notes_off()); assert(panic.held_count() == 1);
  assert(panic.flush(port) >= 1 && panic.held_count() == 0);
  assert(panic.submit(one_on(72))); assert(panic.flush(port) == 1);
  panic.set_connected(false); assert(panic.flush(port) == 0);
  panic.set_connected(true); assert(panic.recovering() && panic.pending() >= 2);
  assert(panic.flush(port) >= 2 && panic.held_count() == 0);
  assert(panic.request_restart() && !panic.recovering());

  // Safety addition to source behavior: a press refused by USB followed by
  // release must never later sound after the release has already drained.
  Output quick;
  fake.refusals = 1;
  assert(quick.submit(one_on(75)) && quick.flush(port) == 0);
  assert(quick.submit(one_off(75)));
  assert(quick.flush(port) == 0 && quick.held_count() == 0);

  // Every accepted voice can retrigger at full polyphony, including release
  // and new press arriving in separate polls before the USB drain.
  Output maximum;
  for (std::uint8_t pitch = 0; pitch < 24; ++pitch) assert(maximum.submit(one_on(pitch)));
  assert(maximum.flush(port) == 24 && maximum.held_count() == 24);
  assert(maximum.submit(one_off(0)));
  assert(maximum.submit(one_on(0)) && !maximum.recovering());
  assert(maximum.flush(port) == 2 && maximum.held_count() == 24);

  Output clock_backlog;
  assert(clock_backlog.queue_clock(96));
  assert(clock_backlog.pending() == 96);
  assert(!clock_backlog.queue_clock() && clock_backlog.recovering());
  assert(clock_backlog.flush(port) == 1); // Recovery Stop, stale clocks discarded.

  // Consecutive clocks are run-length merged but still leave one packet each,
  // in order with respect to surrounding events.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    assert(o.queue_clock(3) && o.queue_clock() && o.submit(one_on(50)) && o.queue_clock(2));
    assert(o.pending() == 7 && o.flush(p) == 7);
    assert(f.packets[3].kind == MidiKind::Clock && f.packets[4].kind == MidiKind::NoteOn);
    assert(f.packets[5].kind == MidiKind::Clock && f.packets[6].kind == MidiKind::Clock);
  }

  // Cross-slot ordering (user-approved improvement over Python, which flushes all
  // offs before all ons and leaves the note stuck). Slot A press then slot B
  // release of a key that was NOT sounding: the pair cancels, nothing is sent.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    assert(o.submit(one_on(61)) && o.submit(one_off(61)));
    assert(o.pending() == 0 && o.flush(p) == 0 && !o.is_sounding(0, 61) && f.count == 0);
    // Same for a key that WAS sounding: A's press is cancelled, the release is
    // sent, and the key ends silent.
    assert(o.submit(one_on(62)) && o.flush(p) == 1 && o.is_sounding(0, 62));
    assert(o.submit(one_on(62)) && o.submit(one_off(62)));
    assert(o.pending() == 1 && o.flush(p) == 1);
    assert(f.count == 2 && f.packets[1].kind == MidiKind::NoteOff && f.packets[1].data1 == 62);
    assert(!o.is_sounding(0, 62) && o.held_count() == 0);
    // Press, release, press, release, press across calls: net one press.
    assert(o.submit(one_on(63)) && o.submit(one_off(63)) && o.submit(one_on(63)));
    assert(o.submit(one_off(63)) && o.submit(one_on(63)));
    assert(o.pending() == 1 && o.flush(p) == 1 && f.packets[2].kind == MidiKind::NoteOn && f.packets[2].data1 == 63);
    // Release from slot A, press from slot B (sounding key): off then on, in order.
    assert(o.submit(one_off(63)) && o.submit(one_on(63, 77)));
    assert(o.flush(p) == 2 && f.packets[3].kind == MidiKind::NoteOff && f.packets[4].kind == MidiKind::NoteOn &&
           f.packets[4].data2 == 77 && o.is_sounding(0, 63));
  }

  // Retrigger inside one call is off,on per note in call order; repeating the
  // retrigger in the same pass collapses to a single off,on pair.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    NoteBatch two{}; two.on.append({64, 90, 0}); two.on.append({65, 91, 0});
    assert(o.submit(two) && o.flush(p) == 2);
    NoteBatch re{}; re.off.append({64, 0, 0}); re.off.append({65, 0, 0});
    re.on.append({64, 80, 0}); re.on.append({65, 81, 0});
    assert(o.submit(re) && o.submit(re) && o.pending() == 4 && o.flush(p) == 4);
    assert(f.packets[2].kind == MidiKind::NoteOff && f.packets[2].data1 == 64);
    assert(f.packets[3].kind == MidiKind::NoteOff && f.packets[3].data1 == 65);
    assert(f.packets[4].kind == MidiKind::NoteOn && f.packets[4].data1 == 64 && f.packets[4].data2 == 80);
    assert(f.packets[5].kind == MidiKind::NoteOn && f.packets[5].data1 == 65 && f.packets[5].data2 == 81);
    assert(o.held_count() == 2);
  }

  // Owed offs survive a full FIFO and refused USB writes, then drain first.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    for (std::uint8_t pitch = 0; pitch < 48; ++pitch) assert(o.submit(one_on(pitch), 0));
    assert(o.flush(p) == 32 && o.flush(p) == 16 && o.held_count() == 48);
    f.count = 0;
    for (std::uint8_t pitch = 0; pitch < 48; ++pitch) assert(o.submit(one_on(pitch), 1));
    assert(o.pending() == 48);
    NoteBatch rel{}; rel.off.append({5, 0, 0}); rel.off.append({6, 0, 0});
    assert(o.submit(rel, 0));  // FIFO full: releases become owed, never dropped
    assert(o.owed_count() == 2 && o.diagnostics().owed_deferred == 2 && o.pending() == 50);
    assert(o.submit(rel, 0) && o.owed_count() == 2);  // repeated request is not double-owed
    assert(o.is_sounding(0, 5) && o.is_off_owed(0, 5));
    f.refusals = 1;  // USB refuses the first owed off: state must not advance
    assert(o.flush(p) == 0 && o.owed_count() == 2 && o.is_sounding(0, 5) && f.count == 0);
    assert(o.diagnostics().refused >= 1);
    assert(o.flush(p) == 32);  // 2 owed offs first, then 30 FIFO presses
    assert(f.packets[0].kind == MidiKind::NoteOff && f.packets[0].channel == 0 && f.packets[0].data1 == 5);
    assert(f.packets[1].kind == MidiKind::NoteOff && f.packets[1].data1 == 6);
    assert(f.packets[2].kind == MidiKind::NoteOn && f.packets[2].channel == 1 && f.packets[2].data1 == 0);
    assert(!o.is_sounding(0, 5) && !o.is_sounding(0, 6) && o.is_sounding(0, 7) && o.owed_count() == 0);
  }
  // A refusal in the middle of an owed drain keeps the rest owed, in order.
  {
    FakeMidi g{}; MidiPort pg{&g, &FakeMidi::send}; Output q;
    for (std::uint8_t pitch = 10; pitch < 13; ++pitch) assert(q.submit(one_on(pitch)));
    assert(q.flush(pg) == 3);
    assert(q.all_notes_off() && q.owed_count() == 3);
    assert(q.flush(pg, 1) == 1 && q.owed_count() == 2 && g.packets[3].data1 == 10);
    g.refusals = 1;
    assert(q.flush(pg) == 0 && q.owed_count() == 2);
    assert(q.flush(pg) == 2 && g.packets[4].data1 == 11 && g.packets[5].data1 == 12 && q.held_count() == 0);
  }

  // A Stop that finds the FIFO full is owed and sent before queued events.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    for (std::uint8_t pitch = 0; pitch < 48; ++pitch) assert(o.submit(one_on(pitch)));
    assert(o.queue_stop() && o.pending() == 49);
    assert(o.flush(p) == 32 && f.packets[0].kind == MidiKind::Stop && f.packets[1].kind == MidiKind::NoteOn);
  }

  // Panic and channel change use the sounding map: they find every sounding
  // identity, drop unsent presses of that channel, leave other channels alone,
  // and do not duplicate an already queued release.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    assert(o.submit(one_on(10), 0) && o.submit(one_on(20), 0) && o.submit(one_on(10), 1));
    assert(o.flush(p) == 3 && o.held_count() == 3);
    assert(o.submit(one_on(30), 0));  // unsent
    assert(!o.all_notes_off(16) && !o.all_notes_off(-2));
    assert(o.all_notes_off(0));
    assert(o.flush(p) == 2 && f.packets[3].kind == MidiKind::NoteOff && f.packets[3].channel == 0 && f.packets[3].data1 == 10);
    assert(f.packets[4].data1 == 20 && !o.is_sounding(0, 30) && o.is_sounding(1, 10) && o.held_count() == 1);
    assert(o.all_notes_off() && o.flush(p) == 1 && f.packets[5].channel == 1 && o.held_count() == 0);
    assert(o.submit(one_on(40)) && o.flush(p) == 1 && o.submit(one_off(40)));  // release already queued
    assert(o.all_notes_off() && o.pending() == 1 && o.flush(p) == 1 && o.held_count() == 0);
    // More than 24 sounding identities across channels are all released.
    for (std::uint8_t pitch = 0; pitch < 40; ++pitch) assert(o.submit(one_on(pitch), static_cast<std::uint8_t>(pitch % 4)));
    assert(o.flush(p) == 32 && o.flush(p) == 8 && o.held_count() == 40);
    assert(o.all_notes_off() && o.owed_count() == 40);
    assert(o.flush(p) == 32 && o.flush(p) == 8 && o.held_count() == 0 && o.owed_count() == 0);
  }

  // Owed releases survive a disconnect and are sent once after reconnect.
  {
    FakeMidi f{}; MidiPort p{&f, &FakeMidi::send}; Output o;
    assert(o.submit(one_on(81)) && o.flush(p) == 1 && o.all_notes_off());
    o.set_connected(false); assert(o.flush(p) == 0 && !o.submit(one_on(1)));
    o.set_connected(true);
    assert(o.flush(p) == 2 && f.packets[1].kind == MidiKind::NoteOff && f.packets[1].data1 == 81 &&
           f.packets[2].kind == MidiKind::Stop);
    assert(o.request_restart() && o.pending() == 0);
  }
  std::printf("sizeof(Output)=%zu\n", sizeof(Output));
}
