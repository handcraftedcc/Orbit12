// T10 portable platform logic: debounce, quadrature, USB-MIDI encode/parse.
#include <cassert>
#include <cstdio>

#include "domain/output.hpp"
#include "platform/macropad/debounce.hpp"
#include "platform/macropad/midi_codec.hpp"
#include "platform/macropad/quadrature.hpp"

using namespace orbit;
using namespace orbit::platform;

static void test_debounce() {
  Debouncer<12> d(5);
  d.reset(0, 0);
  // Bounce: press chatters, never stable for 5 ms -> no edge.
  assert(d.update(1, 0) == 0);
  assert(d.update(0, 1) == 0);
  assert(d.update(1, 2) == 0);
  assert(d.update(0, 3) == 0);
  assert(d.update(1, 4) == 0);
  assert(d.update(1, 8) == 0);            // only 4 ms since last change
  assert(d.update(1, 9) == 1);            // 5 ms stable -> press edge, once
  assert(d.stable() == 1);
  assert(d.update(1, 20) == 0);
  // Release bounce, independent key 3 pressed meanwhile.
  assert(d.update(0, 30) == 0);
  assert(d.update(1 | (1U << 3), 31) == 0);
  assert(d.update(1 | (1U << 3), 36) == (1U << 3));  // key 3 accepted, key 0 still bouncing
  assert(d.update(1 | (1U << 3), 40) == 0);
  assert(d.update(0, 50) == 0);
  assert(d.update(0, 55) == (1U | (1U << 3)));  // both releases settle after 5 ms
  // Boot-held key adopted via reset produces no edge.
  Debouncer<12> held(5);
  held.reset(0x0004, 100);
  assert(held.update(0x0004, 200) == 0 && held.stable() == 0x0004);
  // Wrap-safe millisecond clock.
  Debouncer<1> w(5);
  w.reset(0, 0xFFFFFFFCU);
  assert(w.update(1, 0xFFFFFFFDU) == 0);
  assert(w.update(1, 0xFFFFFFFFU) == 0);
  assert(w.update(1, 0x00000003U) == 1);  // 6 ms across the wrap
  // Switch-style 10 ms interval, 1-bit instance.
  Debouncer<1> sw(10);
  sw.reset(0, 0);
  assert(sw.update(1, 1) == 0 && sw.update(1, 10) == 0 && sw.update(1, 11) == 1);
}

static std::int32_t feed_all(QuadratureDecoder& q, const std::uint8_t* s, int n) {
  std::int32_t sum = 0;
  for (int i = 0; i < n; ++i) sum += q.feed(s[i]);
  return sum;
}

static void test_quadrature() {
  // Gray-code cycles (A<<1|B). ccw: 00 01 11 10 ; cw: 00 10 11 01.
  // cw is the sequence the flashed MacroPad reports for a clockwise turn; it must
  // yield +1 (value up / selection next). The polarity constant kInvert is +1
  // (hardware-verified; the source-derived assumption was -1 and was reversed).
  const std::uint8_t ccw[] = {1, 3, 2, 0};   // one detent (4 quarter steps)
  const std::uint8_t cw[] = {2, 3, 1, 0};
  QuadratureDecoder q;
  q.reset(0);
  const std::int32_t one_ccw = feed_all(q, ccw, 4);
  assert(one_ccw == -1);                     // counter-clockwise -> selection previous
  QuadratureDecoder r;
  r.reset(0);
  const std::int32_t one_cw = feed_all(r, cw, 4);
  assert(one_cw == 1);                       // clockwise -> value up / selection next
  assert(one_cw == -one_ccw);                // direction is symmetric
  // Three detents accumulate; partial detent produces nothing yet.
  QuadratureDecoder m;
  m.reset(0);
  std::int32_t total = 0;
  for (int i = 0; i < 3; ++i) total += feed_all(m, ccw, 4);
  assert(total == 3 * one_ccw);
  assert(feed_all(m, ccw, 3) == 0);
  // Back out the partial detent: no net output.
  const std::uint8_t back[] = {3, 1, 0};
  assert(feed_all(m, back, 2) == 0);
  // Glitchy: a double-bit jump is counted, ignored, state resyncs.
  QuadratureDecoder g;
  g.reset(0);
  assert(g.feed(3) == 0 && g.glitches() == 1);   // 00 -> 11 impossible
  assert(g.feed(3) == 0 && g.feed(3) == 0);      // repeats ignored
  // Chatter on one edge (01,00,01,00) nets zero.
  QuadratureDecoder c;
  c.reset(0);
  const std::uint8_t chatter[] = {1, 0, 1, 0, 1, 0};
  assert(feed_all(c, chatter, 6) == 0);
  // Forward detent after chatter still completes exactly one detent.
  assert(feed_all(c, ccw, 4) == one_ccw);
}

static void test_mailbox() {
  DeltaMailbox mb;
  assert(mb.take() == 0);
  mb.add(1); mb.add(1); mb.add(-1);
  assert(mb.take() == 1);
  assert(mb.take() == 0);
  for (int i = 0; i < 40000; ++i) mb.add(1);
  assert(mb.take() == 32767);           // saturates, remainder retained
  assert(mb.take() == 40000 - 32767);
  for (int i = 0; i < 40000; ++i) mb.add(-1);
  assert(mb.take() == -32768);
  assert(mb.take() == -(40000 - 32768));
  assert(mb.take() == 0);
}

static UsbMidiPacket enc(MidiKind k, std::uint8_t ch, std::uint8_t d1, std::uint8_t d2) {
  UsbMidiPacket p{};
  assert(encode_usb_packet({k, ch, d1, d2}, p));
  return p;
}

static void test_encode() {
  // Channel 0 and 15 map to wire nibble 0 and F; conversion happens only here.
  auto a = enc(MidiKind::NoteOn, 0, 60, 100);
  assert(a.b[0] == 0x09 && a.b[1] == 0x90 && a.b[2] == 60 && a.b[3] == 100);
  auto b = enc(MidiKind::NoteOn, 15, 127, 1);
  assert(b.b[0] == 0x09 && b.b[1] == 0x9F && b.b[2] == 127 && b.b[3] == 1);
  // NoteOff velocity is normalized to zero.
  auto c = enc(MidiKind::NoteOff, 15, 0, 99);
  assert(c.b[0] == 0x08 && c.b[1] == 0x8F && c.b[2] == 0 && c.b[3] == 0);
  auto d = enc(MidiKind::NoteOff, 0, 61, 0);
  assert(d.b[1] == 0x80 && d.b[2] == 61);
  assert(enc(MidiKind::Clock, 0, 0, 0).b[1] == 0xF8);
  assert(enc(MidiKind::Start, 0, 0, 0).b[1] == 0xFA);
  assert(enc(MidiKind::Continue, 0, 0, 0).b[1] == 0xFB);
  assert(enc(MidiKind::Stop, 0, 0, 0).b[1] == 0xFC && enc(MidiKind::Stop, 0, 0, 0).b[0] == 0x0F);
  UsbMidiPacket bad{};
  assert(!encode_usb_packet({MidiKind::NoteOn, 16, 60, 1}, bad));
  assert(!encode_usb_packet({MidiKind::NoteOn, 0, 128, 1}, bad));
  assert(!encode_usb_packet({MidiKind::NoteOn, 0, 60, 128}, bad));
  assert(usb_payload_length(0x9) == 3 && usb_payload_length(0xC) == 2 &&
         usb_payload_length(0xF) == 1 && usb_payload_length(0x0) == 0 && usb_payload_length(0x1) == 0);
}

struct Collected {
  MidiInputKind kinds[32];
  int n = 0;
};
static void parse(MidiByteParser& p, Collected& out, const std::uint8_t* bytes, int count) {
  for (int i = 0; i < count; ++i) {
    MidiInput m;
    if (p.feed(bytes[i], m)) out.kinds[out.n++] = m.kind;
  }
}

static void test_parser() {
  MidiByteParser p;
  Collected o;
  // Plain realtime.
  const std::uint8_t rt[] = {0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF};
  parse(p, o, rt, 6);
  assert(o.n == 4 && o.kinds[0] == MidiInputKind::Clock && o.kinds[1] == MidiInputKind::Start &&
         o.kinds[2] == MidiInputKind::Continue && o.kinds[3] == MidiInputKind::Stop);
  // Partial message split across calls, with a clock inside it.
  Collected q;
  MidiByteParser p2;
  const std::uint8_t part1[] = {0x90, 60};
  const std::uint8_t part2[] = {0xF8, 100};
  parse(p2, q, part1, 2);
  assert(q.n == 0);
  parse(p2, q, part2, 2);
  assert(q.n == 1 && q.kinds[0] == MidiInputKind::Clock);
  assert(p2.ignored() == 1);  // the note-on completed and was ignored
  // Running status: data pairs without a repeated status; realtime between
  // and inside pairs must not disturb it.
  Collected r;
  MidiByteParser p3;
  const std::uint8_t run[] = {0x93, 60, 100, 0xF8, 61, 0xFC, 100, 62, 0, 0xFA};
  parse(p3, r, run, 10);
  assert(r.n == 3 && r.kinds[0] == MidiInputKind::Clock && r.kinds[1] == MidiInputKind::Stop &&
         r.kinds[2] == MidiInputKind::Start);
  assert(p3.ignored() == 3);  // three note messages (two via running status)
  // A new status byte aborts a partial message and never yields a transport event.
  Collected s;
  MidiByteParser p4;
  const std::uint8_t abort_seq[] = {0x90, 60, 0xC0, 5, 0xB0, 7, 1, 0xF8};
  parse(p4, s, abort_seq, 8);
  assert(s.n == 1 && s.kinds[0] == MidiInputKind::Clock && p4.ignored() == 2);
  // SysEx bodies containing data bytes are skipped; realtime still passes.
  Collected x;
  MidiByteParser p5;
  const std::uint8_t sysex[] = {0xF0, 0x7E, 0xF8, 0x01, 0xF7, 0xFC};
  parse(p5, x, sysex, 6);
  assert(x.n == 2 && x.kinds[0] == MidiInputKind::Clock && x.kinds[1] == MidiInputKind::Stop);
  // System common cancels running status: following data bytes are stray.
  Collected y;
  MidiByteParser p6;
  const std::uint8_t sc[] = {0x90, 60, 100, 0xF3, 4, 61, 100, 0xF8};
  parse(p6, y, sc, 8);
  assert(y.n == 1 && p6.ignored() == 2);  // note-on + song select; stray 61,100 dropped
  // Stray data without any status.
  Collected z;
  MidiByteParser p7;
  const std::uint8_t stray[] = {1, 2, 3, 0xF8};
  parse(p7, z, stray, 4);
  assert(z.n == 1);
}

static void test_rx_queue() {
  MidiRxQueue<4> q;
  for (int i = 0; i < 4; ++i) assert(q.push({i % 2 ? MidiInputKind::Start : MidiInputKind::Clock}));
  assert(!q.push({MidiInputKind::Stop}) && q.overflow() == 1 && q.size() == 4);
  q.consume(1);
  assert(q.size() == 3 && q.data()[0].kind == MidiInputKind::Start);
  q.consume(9);
  assert(q.size() == 0);
}

// Refused USB write: Output must keep state honest (no fabricated sounding)
// and deliver the exact packet once the port accepts, with wire encoding.
struct FakeUsb {
  UsbMidiPacket sent[16]{};
  int count = 0;
  int refuse = 0;
  static bool send(void* c, const MidiPacket& p) {
    auto& s = *static_cast<FakeUsb*>(c);
    if (s.refuse > 0) { --s.refuse; return false; }
    return encode_usb_packet(p, s.sent[s.count]) && (++s.count, true);
  }
};

static void test_refused_write() {
  FakeUsb usb;
  MidiPort port{&usb, &FakeUsb::send};
  Output out;
  NoteBatch on{};
  on.on.append({60, 100, 15});
  assert(out.submit(on, 15));  // output channel 15 -> wire nibble F
  usb.refuse = 2;
  out.flush(port);
  assert(usb.count == 0 && out.held_count() == 0 && out.diagnostics().refused >= 1);
  assert(out.pending() >= 1);
  out.flush(port);
  out.flush(port);
  assert(usb.count == 1 && usb.sent[0].b[1] == 0x9F && usb.sent[0].b[2] == 60);
  assert(out.held_count() == 1);
  // Release refused: stays owed until accepted; never lost.
  NoteBatch off{};
  off.off.append({60, 0, 15});
  assert(out.submit(off, 15));
  usb.refuse = 1;
  out.flush(port);
  assert(out.held_count() == 1 || out.owed_count() == 1);
  out.flush(port);
  assert(usb.count == 2 && usb.sent[1].b[1] == 0x8F && usb.sent[1].b[3] == 0);
  assert(out.held_count() == 0 && out.owed_count() == 0);
}

int main() {
  test_debounce();
  test_quadrature();
  test_mailbox();
  test_encode();
  test_parser();
  test_rx_queue();
  test_refused_write();
  std::puts("t10_platform passed");
  return 0;
}
