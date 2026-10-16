#pragma once
// Portable USB-MIDI packet encoding and byte-stream parsing. No Arduino types.
//
// Channel policy: the domain is zero-based (0..15). The wire status nibble is
// also zero-based, so the single conversion point is channel -> status nibble
// in encode_usb_packet(). No 1-based library API is used: raw USB-MIDI event
// packets go through Adafruit_USBD_MIDI::writePacket/readPacket.
#include <cstddef>
#include <cstdint>

#include "domain/output.hpp"
#include "domain/transport.hpp"

namespace orbit {
namespace platform {

struct UsbMidiPacket { std::uint8_t b[4]; };

// Returns false (and leaves out untouched) for a packet the wire cannot carry.
// NoteOff velocity is normalized to 0 here.
inline bool encode_usb_packet(const MidiPacket& p, UsbMidiPacket& out) noexcept {
  switch (p.kind) {
    case MidiKind::NoteOff:
    case MidiKind::NoteOn: {
      if (p.channel > kMidiChannelMax || p.data1 > kMidiMax || p.data2 > kMidiMax) return false;
      const bool on = p.kind == MidiKind::NoteOn;
      const std::uint8_t status = static_cast<std::uint8_t>((on ? 0x90U : 0x80U) | p.channel);
      out = {{static_cast<std::uint8_t>(on ? 0x09U : 0x08U), status, p.data1,
              static_cast<std::uint8_t>(on ? p.data2 : 0U)}};
      return true;
    }
    case MidiKind::Clock: out = {{0x0F, 0xF8, 0, 0}}; return true;
    case MidiKind::Start: out = {{0x0F, 0xFA, 0, 0}}; return true;
    case MidiKind::Continue: out = {{0x0F, 0xFB, 0, 0}}; return true;
    case MidiKind::Stop: out = {{0x0F, 0xFC, 0, 0}}; return true;
  }
  return false;
}

// Number of payload bytes (1..3) in a USB-MIDI event packet for its CIN;
// 0 for reserved CINs.
inline std::uint8_t usb_payload_length(std::uint8_t cin) noexcept {
  switch (cin & 0x0FU) {
    case 0x2: case 0x6: case 0xC: case 0xD: return 2;
    case 0x3: case 0x4: case 0x7: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: return 3;
    case 0x5: case 0xF: return 1;
    default: return 0;
  }
}

// Byte-stream parser keeping only what the transport needs (Clock/Start/
// Continue/Stop). Handles partial messages, running status and realtime bytes
// interleaved inside other messages (which never disturb running status or
// the partial message). Everything else is consumed and counted as ignored.
class MidiByteParser {
 public:
  // Returns true when `out` holds a transport-relevant message.
  bool feed(std::uint8_t byte, MidiInput& out) noexcept {
    if (byte >= 0xF8) {
      switch (byte) {
        case 0xF8: out.kind = MidiInputKind::Clock; return true;
        case 0xFA: out.kind = MidiInputKind::Start; return true;
        case 0xFB: out.kind = MidiInputKind::Continue; return true;
        case 0xFC: out.kind = MidiInputKind::Stop; return true;
        default: ++ignored_; return false;  // F9/FD undefined, FE/FF not transport
      }
    }
    if (byte & 0x80U) {
      have_ = 0;
      if (byte < 0xF0) {
        running_ = byte;
        need_ = static_cast<std::uint8_t>(((byte & 0xF0U) == 0xC0U || (byte & 0xF0U) == 0xD0U) ? 1 : 2);
        sysex_ = false;
      } else if (byte == 0xF0) {
        running_ = 0; need_ = 0; sysex_ = true;
      } else {  // F1..F7 system common: cancels running status
        running_ = 0; sysex_ = false;
        need_ = static_cast<std::uint8_t>((byte == 0xF1 || byte == 0xF3) ? 1 : byte == 0xF2 ? 2 : 0);
        if (need_ == 0) ++ignored_;
      }
      return false;
    }
    if (sysex_ || need_ == 0) return false;  // sysex body or stray data byte
    if (++have_ >= need_) {
      have_ = 0;
      ++ignored_;  // complete non-transport message; running status stays
      if (running_ == 0) need_ = 0;
    }
    return false;
  }
  std::uint32_t ignored() const noexcept { return ignored_; }

 private:
  std::uint8_t running_ = 0, need_ = 0, have_ = 0;
  bool sysex_ = false;
  std::uint32_t ignored_ = 0;
};

// Bounded linear queue of received transport messages. The consumer reads
// data()/size(), then consume(n) (matches Transport::update's contract).
template <std::size_t N>
class MidiRxQueue {
 public:
  bool push(const MidiInput& m) noexcept {
    if (count_ >= N) { ++overflow_; return false; }
    items_[count_++] = m;
    return true;
  }
  const MidiInput* data() const noexcept { return items_; }
  std::size_t size() const noexcept { return count_; }
  void consume(std::size_t n) noexcept {
    if (n >= count_) { count_ = 0; return; }
    for (std::size_t i = n; i < count_; ++i) items_[i - n] = items_[i];
    count_ -= n;
  }
  std::uint32_t overflow() const noexcept { return overflow_; }

 private:
  MidiInput items_[N]{};
  std::size_t count_ = 0;
  std::uint32_t overflow_ = 0;
};

}  // namespace platform
}  // namespace orbit
