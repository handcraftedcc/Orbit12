# T00 Arduino hardware and toolchain evidence

Status: **corrected USB initialization compiles; device retest pending**. This is a hardware smoke test, not a validated Arduino port. This agent did not access or flash the device.

## Resolved toolchain and profile

- Arduino CLI `1.5.1` was downloaded from the official GitHub release to the ignored local cache. Its official Windows x64 archive SHA-256 was verified as `fabe42e0eb04d00e776a66178299ff95a46c623dbc260f997e58fd514853dd40` against the release checksum manifest. No global installation or PATH change was made.
- All CLI data, packages, downloads, build outputs, and UF2 artifacts are under `tools/arduino/.tool-cache/`; `tools/arduino/.gitignore` excludes this cache. The repeatable bootstrap is `tools/arduino/bootstrap.ps1`; `tools/arduino/check-environment.ps1` uses the workspace-local CLI first.
- Arduino-Pico `rp2040:rp2040` `6.3.0` was resolved from its official board index and installed locally. Board details reported FQBN `rp2040:rp2040:adafruit_macropad2040`, flash option `8MB (Sketch: 7MB, FS: 1MB)`, default CPU `200 MHz`, and USB option `Adafruit TinyUSB` (`usbstack=tinyusb`). The 6.3.0 core archive metadata reported SHA-256 `dddf70013f895e369e16042b49958ac5334ae81dab9573262d348354a97f6e09`.
- Library Manager versions resolved and installed for the smoke profile: Adafruit TinyUSB Library `3.7.7`, MIDI Library `5.0.2`, Adafruit SH110X `2.1.15`, Adafruit GFX Library `1.12.6`, Adafruit BusIO `1.17.4`, Adafruit NeoPixel `1.15.5`, Adafruit SPIFlash `5.1.1`, and SdFat - Adafruit Fork `2.3.103` (the last two are TinyUSB dependencies).
- The profile is recorded in `firmware/Orbit12/sketch.yaml` and duplicated alongside the isolated sketch at `firmware/Orbit12/T00HardwareSpike/sketch.yaml` so the smoke can be built independently.

## Verified hardware references

- Adafruit's MacroPad Arduino example uses `Adafruit_SH1106G(128, 64, &SPI1, OLED_DC, OLED_RST, OLED_CS)`, built-in `NUM_NEOPIXEL` / `PIN_NEOPIXEL`, `PIN_ROTA` / `PIN_ROTB`, `PIN_SWITCH`, and GPIO 1–12 for keys. Source: [Adafruit MacroPad Arduino example](https://learn.adafruit.com/adafruit-macropad-rp2040/arduino).
- The installed Arduino-Pico board metadata reports 8 MB flash and selectable CPU speeds; the selected 200 MHz setting is the board default at core 6.3.0. Options identify TinyUSB (`usbstack=tinyusb`) and a 1 MiB filesystem partition (`flash=8388608_1048576`). This corrects the earlier discovery note's 250 MHz assumption for this core release; no overclock option was selected.
- Arduino CLI profile syntax is documented in [Arduino CLI sketch project files](https://arduino.github.io/arduino-cli/0.32/sketch-project-file/). The official core package index is [Arduino-Pico package index](https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json).

## Spike and ownership

`firmware/Orbit12/T00HardwareSpike/T00HardwareSpike.ino` is isolated from the final sketch. It assigns TinyUSB/MIDI receive and transmit to core 0, and GPIO, SPI SH1106, and NeoPixel access to core 1. Startup uses a release/acquire readiness flag; neither core spins or waits. The spike emits one test note-on/off pair for a later duplex MIDI device check. Core 1 draws the existing 128x64 launch bitmap from `circuitpython-source/UI_Design/LaunchScreen1.1.bmp`; its source SHA-256 is `5d17eaf2c285d9fb3dfec2877c45ca0d138bff4975a18d4aece0ee47b6de0073`.

`firmware/Orbit12/Orbit12.ino` contains only the final application's core-owned entry-point hooks. Application adapters and domains are not stubbed or advertised as implemented.

## Verification record

| Check | Result |
| --- | --- |
| CLI/core/library discovery | Passed with workspace-local CLI 1.5.1, Arduino-Pico 6.3.0, and exact library versions above |
| Core 0 TinyUSB MIDI RX/TX compile | Passed; `Midi.read()`, one NoteOn, then NoteOff compile on core 0 |
| Core 1 GPIO/SPI SH1106/NeoPixel compile | Passed; uses MacroPad GPIO macros and embedded original 128x64 launch bitmap on core 1 |
| Core setup / atomic handshake compile | Passed; target compiled release/acquire 32-bit atomic readiness flag |
| Target memory report | 95,424 bytes program storage of 7,335,936; 17,796 bytes static RAM of 262,144 |
| Initial UF2 | `tools/arduino/.tool-cache/artifacts/t00-smoke/T00HardwareSpike.ino.uf2`, 226,816 bytes, SHA-256 `f2115827a3e300afbfb8baa999d8f916fe969a0d4cac60fae085c49e5c9d2ac3`; device test failed with Windows ConfigManagerErrorCode 10 and VID_239A/PID_CAFE, with no serial or MIDI endpoints |
| Corrected UF2 | `tools/arduino/.tool-cache/artifacts/t00-usb-fix/T00HardwareSpike.ino.uf2`, 226,816 bytes, SHA-256 `9df9c6cc9f3ad0d8e1ff5ecb389bcc3a1348bd963005cdaa6ffcd5fa7ca7c5e5` |
| MacroPad device input/display/LED run | Pending: requires loading the built image and visual/input check; no flash performed |
| USB MIDI input/output host loopback | Pending: requires device and host capture; no flash performed |
| Flash partition and LittleFS behavior | Compile-time profile selects 1 MiB; on-device persistence behavior remains pending |
| Flashing | Coordinator flashed the initial UF2 for diagnosis; this agent did not access or write the device. Corrected image not yet flashed. |

## USB failure diagnosis and correction

The initial device result was a composite device with VID_239A/PID_CAFE, Windows device error 10, and no serial/MIDI interfaces. Inspection of the installed Arduino-Pico 6.3.0 source shows `cores/rp2040/main.cpp:148` calls `TinyUSB_Device_Init(0)` before sketch `setup()` when `usbstack=tinyusb` is selected. The installed Adafruit TinyUSB MIDI example checks `TinyUSBDevice.isInitialized()` before manually beginning TinyUSB, registers MIDI through `MIDI.begin()`, and detaches/reattaches if the device already mounted. The initial spike instead called `TinyUSBDevice.begin()` unconditionally, which resets its configuration and initializes the port again after the core's initialization.

The corrected spike now follows that installed example: it calls `TinyUSBDevice.begin(0)` only if not already initialized, sets the MIDI cable string descriptor before registration, registers the MIDI interface through `Midi.begin()`, and requests detach/reattach if already mounted. The corrected image compiles; only the device retest can establish whether Windows now sees valid CDC and MIDI interfaces. This diagnosis is evidence-backed but not yet device-confirmed.

The target smoke is not application firmware and must not be presented as a complete controller. It proves build feasibility and compile-time core ownership only. Device-level GPIO, display, NeoPixel, USB MIDI, and LittleFS behavior remain unverified.
