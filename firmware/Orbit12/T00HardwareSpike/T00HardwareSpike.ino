#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include <atomic>
#include "launchscreen_bitmap.h"

// Isolated feasibility spike. This is deliberately not the Orbit12 app.
Adafruit_USBD_MIDI usbMidi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidi, Midi);

Adafruit_SH1106G display(128, 64, &SPI1, OLED_DC, OLED_RST, OLED_CS);
Adafruit_NeoPixel pixels(NUM_NEOPIXEL, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

static std::atomic<uint32_t> uiReady{0};
static uint32_t receivedMidiMessages = 0;
static uint32_t lastTransmitMs = 0;
static uint8_t testMidiPhase = 0;
static uint32_t lastScanMs = 0;
static uint32_t lastDisplayMs = 0;
static int32_t encoderEdges = 0;
static uint8_t encoderPrevious = 0;
static bool diagnosticsVisible = false;

void setup() {
  // USB/TinyUSB and MIDI are initialized and used only on core 0.
  // Arduino-Pico's core initializes TinyUSB before setup() for this profile.
  // Match Adafruit's MIDI example and avoid resetting the already-running stack.
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usbMidi.setStringDescriptor("Orbit12 MIDI");
  Midi.begin(MIDI_CHANNEL_OMNI);
  Midi.turnThruOff();
  Serial.begin(115200);

  // The core may enumerate its CDC interface before this sketch registers MIDI.
  // Re-enumerate after the MIDI descriptor has been added.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void loop() {
  if (Midi.read()) ++receivedMidiMessages; // no MIDI-path serial logging
  // Explicit host-side smoke commands only: T triggers the one-note TX probe,
  // ? returns a bounded diagnostic counter so duplex MIDI can be verified.
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 'T') testMidiPhase = 0;
    if (command == '?' && Serial.availableForWrite() >= 32) {
      Serial.print("T00 RX="); Serial.println(receivedMidiMessages);
    }
  }

  if (uiReady.load(std::memory_order_acquire) != 0 && testMidiPhase == 0) {
    Midi.sendNoteOn(60, 1, 1);  // one explicit TX smoke packet, channel API is 1-based
    lastTransmitMs = millis();
    testMidiPhase = 1;
  }
  if (testMidiPhase == 1 && millis() - lastTransmitMs >= 100) {
    Midi.sendNoteOff(60, 0, 1);
    testMidiPhase = 2;
  }
}

void setup1() {
  // Board/library objects below are owned exclusively by core 1.
  pixels.begin();
  pixels.clear();
  pixels.show();

  display.begin(0, true);
  display.clearDisplay();
  // Original BMP palette: index 0 = white, index 1 = black. drawBitmap's
  // foreground applies to set bits (index 1), so retain that palette mapping.
  display.drawBitmap(0, 0, kLaunchScreenBitmap, 128, 64, SH110X_BLACK, SH110X_WHITE);
  display.display();

  for (uint8_t pin = 1; pin <= 12; ++pin) pinMode(pin, INPUT_PULLUP);
  pinMode(PIN_ROTA, INPUT_PULLUP);
  pinMode(PIN_ROTB, INPUT_PULLUP);
  pinMode(PIN_SWITCH, INPUT_PULLUP);
  encoderPrevious = (digitalRead(PIN_ROTA) << 1) | digitalRead(PIN_ROTB);

  uiReady.store(1, std::memory_order_release);
}

void loop1() {
  // Keep board GPIO/SPI/NeoPixel accesses on core 1. The test intentionally
  // performs bounded work and contains no cross-core wait.
  static constexpr int8_t transitions[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  const uint8_t encoderCurrent = (digitalRead(PIN_ROTA) << 1) | digitalRead(PIN_ROTB);
  encoderEdges += transitions[(encoderPrevious << 2) | encoderCurrent];
  encoderPrevious = encoderCurrent;
  const uint32_t now = millis();
  if (now - lastScanMs < 3) return;
  lastScanMs = now;
  uint16_t mask = 0;
  for (uint8_t key = 0; key < 12; ++key) {
    const bool down = digitalRead(key + 1) == LOW;
    if (down) mask |= uint16_t(1) << key;
    pixels.setPixelColor(key, down ? pixels.Color(24, 24, 24) : 0);
  }
  const bool encoderDown = digitalRead(PIN_SWITCH) == LOW;
  pixels.show();
  if (mask || encoderDown || encoderEdges) diagnosticsVisible = true;
  // Burn-in protection: sleep the OLED after 60 s without input (matches
  // Orion.py SCREEN_SLEEP_TIME), wake on any key/switch/encoder activity.
  static uint16_t lastMask = 0; static bool lastDown = false; static int32_t lastEdges = 0;
  static uint32_t lastActivityMs = 0; static bool asleep = false;
  if (mask != lastMask || encoderDown != lastDown || encoderEdges != lastEdges) {
    lastMask = mask; lastDown = encoderDown; lastEdges = encoderEdges; lastActivityMs = now;
    if (asleep) { display.oled_command(SH110X_DISPLAYON); asleep = false; }
  } else if (!asleep && now - lastActivityMs > 60000UL) {
    display.oled_command(SH110X_DISPLAYOFF); asleep = true;
  }
  if (asleep) return;
  if (!diagnosticsVisible || now - lastDisplayMs < 34) return;
  lastDisplayMs = now;
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("T00 KEY/ENCODER TEST");
  display.setCursor(0, 16);
  display.print("KEY MASK: "); display.print(mask, HEX);
  display.setCursor(0, 32);
  display.print("ENC EDGES: "); display.print(encoderEdges);
  display.setCursor(0, 48);
  display.print("SWITCH: "); display.print(encoderDown ? "DOWN" : "UP");
  display.display();
}
