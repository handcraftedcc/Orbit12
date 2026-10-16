#include "platform/macropad/usb_midi.hpp"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

namespace orbit {
namespace platform {
namespace {
Adafruit_USBD_MIDI g_usb_midi;
bool send_thunk(void* ctx, const MidiPacket& packet) {
  return static_cast<UsbMidi*>(ctx)->try_send(packet);
}
}  // namespace

bool UsbMidi::begin(bool start_cdc_serial) noexcept {
  // Arduino-Pico initializes TinyUSB before setup() with this profile; only
  // initialize if it has not happened (matches the verified T00 spike).
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  g_usb_midi.setStringDescriptor("Orbit12 MIDI");
  const bool ok = g_usb_midi.begin();
  if (start_cdc_serial) Serial.begin(115200);
  if (TinyUSBDevice.mounted()) {  // re-enumerate so the host sees the MIDI interface
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  return ok;
}

bool UsbMidi::connected() const noexcept { return TinyUSBDevice.mounted(); }

MidiPort UsbMidi::port() noexcept { return {this, &send_thunk}; }

bool UsbMidi::try_send(const MidiPacket& packet) noexcept {
  UsbMidiPacket raw;
  if (!encode_usb_packet(packet, raw)) { ++diag_.tx_invalid; return true; }
  if (!g_usb_midi.writePacket(raw.b)) { ++diag_.tx_refused; return false; }
  return true;
}

void UsbMidi::poll() noexcept {
  uint8_t raw[4];
  for (std::uint8_t n = 0; n < kPollPacketLimit; ++n) {
    if (!g_usb_midi.readPacket(raw)) break;
    const std::uint8_t length = usb_payload_length(raw[0]);
    if (length == 0) { ++diag_.rx_reserved; continue; }
    for (std::uint8_t i = 0; i < length; ++i) {
      MidiInput message;
      if (parser_.feed(raw[1 + i], message)) rx_.push(message);
    }
  }
}

UsbMidiDiagnostics UsbMidi::diagnostics() const noexcept {
  UsbMidiDiagnostics d = diag_;
  d.rx_overflow = rx_.overflow();
  d.rx_ignored = parser_.ignored();
  return d;
}

}  // namespace platform
}  // namespace orbit
