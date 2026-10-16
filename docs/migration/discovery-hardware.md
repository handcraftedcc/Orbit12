# Hardware and dependency discovery

## Current target and hardware contract

Orbit12 currently targets the Adafruit MacroPad RP2040 only (README). Its CircuitPython wrapper is explicitly board-specific and rejects nonzero display/key rotation (`circuitpython-source/src/apps/Orion/core/orion_macropad.py`). The hardware is an RP2040 with 264 KB SRAM and 8 MB QSPI flash; the MacroPad adds twelve independent key inputs, twelve NeoPixels, a rotary encoder and push switch, and a 128x64 SH1106 monochrome OLED on hardware SPI. The board is suitable for Arduino: Adafruit's setup guide directs users to the Philhower RP2040 Arduino core, and its Arduino pinout maps the keys to pins 1–12 and provides `PIN_ROTA`, `PIN_ROTB`, `PIN_SWITCH`, `PIN_NEOPIXEL`, and OLED wiring. [Adafruit overview and pinouts](https://learn.adafruit.com/adafruit-macropad-rp2040/pinouts) [Adafruit Arduino usage](https://learn.adafruit.com/adafruit-macropad-rp2040/arduino-usage)

The current wrapper's concrete hardware use is:

| Current CircuitPython API | Actual behavior | Arduino direction |
| --- | --- | --- |
| `keypad.Keys(KEY1..KEY12, value_when_pressed=False, pull=True)` | Polls twelve active-low independent switches | GPIO 1–12 as `INPUT_PULLUP`; scan or use an event/debounce layer |
| `rotaryio.IncrementalEncoder(ROTA, ROTB)` | Reads encoder position; app negates it | Arduino-Pico `Encoder`/GPIO interrupt or polling implementation; preserve sign and step behavior |
| `DigitalInOut(BUTTON)` + `adafruit_debouncer.Debouncer` | Active-low encoder switch, debounced pressed/released edges | GPIO input pull-up and explicit debounce/state-edge logic |
| `neopixel.NeoPixel(NEOPIXEL, 12)` | Per-key RGB indicators | Adafruit NeoPixel or compatible RP2040 driver using `PIN_NEOPIXEL` |
| `board.DISPLAY` / `displayio` / `vectorio` / on-disk BMPs | Direct SH1106 display object and retained tile/bitmap UI | SH1106 + GFX driver and a rewritten renderer; current `displayio` scene graph and BMP asset interface do not port directly |
| `display.bus.send(0xAE/0xAF, b"")` | Sends OLED sleep/wake commands | Preserve as SH1106 display command calls if sleep behavior remains needed |

The key switches are independent GPIO inputs rather than a row/column matrix. The display is SH1106, not SSD1306, and uses SPI. Use the board variant's pin constants (and verify the exact SPI/DC/CS/reset names against that variant) instead of assuming generic Pico defaults. [Adafruit pinout guide](https://learn.adafruit.com/adafruit-macropad-rp2040/pinouts)

## MIDI transport and timing

`OrionMacroPad` constructs `adafruit_midi.MIDI` with `usb_midi.ports[0]` for input and `[1]` for output. `Transport` receives USB MIDI messages, recognizes `TimingClock`, `Start`, `Stop`, and `Continue`, and uses 24 MIDI clock ticks per quarter note. Internal mode emits note-on/off plus MIDI clock, Start, and Stop messages; external mode advances on incoming clock. `OutputManager` flushes note-offs before note-ons. There is no DIN MIDI/UART transport in this code: the actual external interface is USB device MIDI.

For Arduino-Pico, the board core has explicit USB support for MIDI through TinyUSB. Its documented USB API provides `tusb-midi.h`; it also offers the Adafruit TinyUSB stack. Adafruit's TinyUSB MIDI example uses `Adafruit_USBD_MIDI` and the FortySevenEffects MIDI library, showing a viable bidirectional MIDI device pattern. [Arduino-Pico USB docs](https://arduino-pico.readthedocs.io/en/latest/usb.html) [Adafruit TinyUSB MIDI example](https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/MIDI/midi_test/midi_test.ino)

The MIDI conversion needs explicit semantic checks. Adafruit CircuitPython MIDI uses zero-based channel values (`in_channel=midi_in_channel-1`; output channels are stored as zero-based values), while the common Arduino MIDI Library API's channel arguments are one-based. Add one at the Arduino boundary for channel voice messages, and keep real-time system messages such as Clock/Start/Stop channel-less. Confirm incoming status parsing and message queue behavior with a USB MIDI loopback prototype. The app drains inbound MIDI even in internal mode, so the port must continue reading while generating output.

`adafruit_ticks` is used throughout the scheduler and modules for wrap-safe millisecond arithmetic (`ticks_ms`, `ticks_diff`, `ticks_add`, and `ticks_less`). Arduino `millis()` is the obvious equivalent, but timing edge behavior and wraparound must be kept explicit. The current loop queues pending clock ticks, then flushes them; replacing it with a blocking send loop could change scheduler latency and MIDI jitter.

## Software dependencies found in the deployment tree

Runtime imports outside the language/core are:

- `adafruit_midi` for USB MIDI message classes and packing/parsing.
- `adafruit_ticks` for wrap-safe timing.
- `adafruit_debouncer.Debouncer` for encoder-switch edge detection.
- `neopixel` (and its `adafruit_pixelbuf` support) for the 12-pixel chain.
- `rainbowio.colorwheel` for color conversion.
- `displayio`, `vectorio`, and `keypad`, which are CircuitPython built-ins rather than separate libraries.

The checked-in `circuitpython-source/src/lib` and `circuitpython-source/src_mpy/lib` trees also include `adafruit_bitmap_font`, `adafruit_display_text`, `adafruit_hid`, `adafruit_macropad`, and `adafruit_simple_text_display`; those are not imported by the current Python application code found in `circuitpython-source/src/`. This distinction matters when deciding what truly needs a C++ replacement. `adafruit_midi`, ticks, debounce, and NeoPixel code are bundled in the current CircuitPython deployment tree as compiled `.mpy` modules; their exact upstream release versions are not recorded there, so they cannot currently be reconstructed as a version-accurate dependency lock.

Likely Arduino components are Arduino-Pico core (board variant and RP2040 USB integration), Adafruit TinyUSB (MIDI device transport), FortySevenEffects MIDI Library (MIDI message handling), Adafruit SH110x + Adafruit GFX (OLED), Adafruit NeoPixel, and small local implementations for tick arithmetic/debounce/encoder input. This is a candidate list, not yet a tested or pinned dependency set.

## Build and pinning recommendation

Use Arduino CLI's `sketch.yaml` build profile to record the MacroPad FQBN, the Arduino-Pico platform version, and exact Library Manager versions. Arduino CLI documents profiles as isolated, reproducible builds with explicit board/library versions. [Arduino CLI sketch project file](https://arduino.github.io/arduino-cli/0.24/sketch-project-file/)

Do not select arbitrary current versions in this discovery note. First compile and exercise a small hardware spike for (1) MacroPad variant GPIO constants, (2) SH1106 SPI initialization and refresh, and (3) simultaneous USB MIDI input/output with the selected USB stack. Then record those successfully tested versions in the profile. Keep the USB stack choice in project documentation/configuration because the Arduino-Pico core offers both its Pico-SDK USB path and Adafruit TinyUSB, and their setup requirements differ. [Arduino-Pico USB docs](https://arduino-pico.readthedocs.io/en/latest/usb.html)

## Constraints and open questions

- Arduino support exists for the same board, so this is not a board migration. Arduino-Pico's current board table includes the Adafruit MacroPad RP2040 with its own board ID and 8 MB flash. [Arduino-Pico supported boards](https://github.com/earlephilhower/arduino-pico/blob/master/README.md)
- SRAM remains 264 KB. CircuitPython's memory-driven lazy-import and fixed-array architecture documents an existing tight memory budget; C++ may reduce runtime overhead, but the graphics buffers, MIDI queues, module state, and flash storage design still require measurement. The CircuitPython assumption that `.mpy` files reduce pressure does not carry over to Arduino.
- `boot.py` samples all keys before startup and remounts the CircuitPython filesystem writable only when no key is held, using held keys for USB file access. Arduino firmware will not expose the `CIRCUITPY` filesystem through this mechanism. Decide separately whether a comparable user-accessible configuration/firmware update mode is a product requirement.
- Scene and settings persistence currently uses text files under `/userdata/Orbit12Orion/`; porting it requires a flash filesystem or a replacement persistence format. The choice must account for power-loss safety and flash wear.
- `microcontroller.cpu.frequency = 250_000_000` is set at app import. Arduino-Pico board metadata lists a 250 MHz default for this board, but confirm actual runtime clock configuration and whether any timing behavior relies on the Python-side assignment. [Arduino-Pico board table](https://github.com/earlephilhower/arduino-pico/blob/master/tools/makeboards.py)
- USB enumeration identity, port count/order, input buffer depth, and send/receive scheduling can differ between CircuitPython and the chosen Arduino USB stack. Confirm host-visible MIDI ports and clock performance on Windows/macOS/Linux before treating transport parity as complete.
- The present workspace did not have `arduino-cli` available during this discovery; no Arduino compile or device test was performed. This is a source-based feasibility assessment, not a validated port.
