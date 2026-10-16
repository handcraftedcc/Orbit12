#pragma once
// Shared rig for the T11b controller suites: a controller, a bridge, a
// capturing MIDI port and a deterministic clock. Controllers are large, so
// rigs are always heap allocated by the tests (never on the stack).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "app/controller.hpp"

extern int g_failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

namespace t11b {
using namespace orbit;

struct Capture {
  std::vector<MidiPacket> packets;
  bool accept = true;
  static bool send(void* ctx, const MidiPacket& p) {
    auto& self = *static_cast<Capture*>(ctx);
    if (!self.accept) return false;
    self.packets.push_back(p);
    return true;
  }
  MidiPort port() { MidiPort p; p.context = this; p.try_send = &Capture::send; return p; }
  std::size_t count(MidiKind kind) const {
    std::size_t n = 0;
    for (const auto& p : packets) if (p.kind == kind) ++n;
    return n;
  }
  std::size_t count(MidiKind kind, std::uint8_t pitch) const {
    std::size_t n = 0;
    for (const auto& p : packets) if (p.kind == kind && p.data1 == pitch) ++n;
    return n;
  }
  void clear() { packets.clear(); }
};

struct Rig {
  Controller c;
  PhysicalEventBridge bridge;
  Capture cap;
  MidiPort port;
  std::uint32_t ms = 1000;

  explicit Rig(const ControllerPorts& ports = {}) : c(ports) {
    port = cap.port();
    c.init(time());
  }
  TimeSample time() const { return {ms, ms * 1000U}; }
  PollArgs args() { PollArgs a; a.now = time(); a.port = &port; return a; }

  PhysicalEvent event(InputKind kind, std::uint8_t key, std::int16_t value) const {
    PhysicalEvent e{};
    e.kind = kind; e.key = key; e.value = value; e.at_ms = ms;
    return e;
  }
  // Direct (single-threaded) injection through the controller.
  void key_down(std::uint8_t k) { c.handle_event(event(InputKind::KeyEdge, k, 1)); }
  void key_up(std::uint8_t k) { c.handle_event(event(InputKind::KeyEdge, k, 0)); }
  void enc_down() { c.handle_event(event(InputKind::EncoderSwitch, 0, 1)); }
  void enc_up() { c.handle_event(event(InputKind::EncoderSwitch, 0, 0)); }
  void turn(int d) { c.handle_event(event(InputKind::EncoderDelta, 0, static_cast<std::int16_t>(d))); }
  // One controller pass, advancing time by dt milliseconds.
  void step(std::uint32_t dt = 1) { ms += dt; (void)c.step(args()); }
  // Encoder held + key press/release + encoder release, one pass after each.
  void combo(std::uint8_t k) { enc_down(); step(); key_down(k); step(); key_up(k); step(); enc_up(); step(); }
  // Hold encoder, press key (shortcut), release key, keep encoder held.
  void chord_key(std::uint8_t k) { key_down(k); step(); key_up(k); step(); }
  const UiModel& model() { return c.model(); }
};

inline const ParamRow* row(const Controller& c, std::uint8_t slot, const char* name) {
  return c.find_row(slot, name);
}

}  // namespace t11b
