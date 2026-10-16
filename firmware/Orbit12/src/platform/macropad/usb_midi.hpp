#pragma once
// Nonblocking USB MIDI duplex over Adafruit TinyUSB, owned by core 0.
// Thru is not implemented (nothing is forwarded). Arduino includes: .cpp only.
#include <cstdint>

#include "../../domain/output.hpp"
#include "midi_codec.hpp"

namespace orbit {
namespace platform {

struct UsbMidiDiagnostics {
  std::uint32_t tx_refused = 0;    // endpoint closed or FIFO full (packet retained by Output)
  std::uint32_t tx_invalid = 0;    // unencodable packet consumed (domain bug, never wedges queue)
  std::uint32_t rx_overflow = 0;   // transport message dropped, queue full
  std::uint32_t rx_ignored = 0;    // non-transport bytes/messages parsed and discarded
  std::uint32_t rx_reserved = 0;   // reserved-CIN packets skipped
};

class UsbMidi {
 public:
  static constexpr std::size_t kRxCapacity = 128;
  static constexpr std::uint8_t kPollPacketLimit = 32;

  // Core 0, once at startup (may briefly re-enumerate; not performance path).
  // start_cdc_serial: begin the CDC port after MIDI registration and before the
  // re-enumeration, the order verified in the T00 spike.
  bool begin(bool start_cdc_serial = false) noexcept;
  // Drains at most kPollPacketLimit raw packets; always call, even in
  // internal-clock mode, so the host never stalls on a full endpoint.
  void poll() noexcept;
  bool connected() const noexcept;  // mounted by a host
  // try_send() is the MidiPort implementation. False == refused (port busy).
  MidiPort port() noexcept;
  bool try_send(const MidiPacket& packet) noexcept;

  const MidiInput* rx_data() const noexcept { return rx_.data(); }
  std::size_t rx_size() const noexcept { return rx_.size(); }
  void rx_consume(std::size_t n) noexcept { rx_.consume(n); }
  UsbMidiDiagnostics diagnostics() const noexcept;

 private:
  MidiByteParser parser_;
  MidiRxQueue<kRxCapacity> rx_;
  UsbMidiDiagnostics diag_;
};

}  // namespace platform
}  // namespace orbit
