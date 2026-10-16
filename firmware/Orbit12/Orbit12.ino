// Orbit12 application glue (T14 composition).
//
// Core 0 (setup/loop):   LittleFS mount (no format) -> USB MIDI poll -> Transport -> Controller
//                        (boot scene load, scene switches, maintenance) -> Output flush;
//                        CDC serial: '?' diagnostics dump in performance, frames while in
//                        maintenance.
// Core 1 (setup1/loop1): GPIO/encoder scan -> bridge events, NeoPixels, SH1106 with 60 s sleep;
//                        acknowledges core-0 quiesce requests and idles until resume.
// Peripherals are reached only through src/platform/macropad adapters; this file is glue.
#include <Arduino.h>

#include "src/app/app_main.hpp"
#include "src/platform/macropad/gpio.hpp"
#include "src/platform/macropad/littlefs_storage.hpp"
#include "src/platform/macropad/usb_midi.hpp"

namespace {
orbit::SharedLink g_link;
orbit::platform::UsbMidi g_usb;
orbit::platform::GpioInput g_gpio;
orbit::LittleFsStorage g_fs;

bool mount_fs(void* context) noexcept {
  return static_cast<orbit::LittleFsStorage*>(context)->mount();
}
void set_fs_window(void* context, bool open) noexcept {
  static_cast<orbit::LittleFsStorage*>(context)->set_maintenance_window(open);
}
orbit::fsformat::FormatResult format_fs(void* context) noexcept {
  return static_cast<orbit::LittleFsStorage*>(context)->format();
}
bool mounted_fs(void* context) noexcept {
  return static_cast<orbit::LittleFsStorage*>(context)->mounted();
}
orbit::AppStorage make_storage() noexcept {
  orbit::AppStorage storage;
  storage.context = &g_fs;
  storage.mount = &mount_fs;
  storage.set_window = &set_fs_window;
  storage.format = &format_fs;
  storage.mounted = &mounted_fs;
  storage.port = g_fs.port();
  return storage;
}
const orbit::AppStorage g_storage = make_storage();  // after g_fs (same translation unit)

orbit::EngineApp g_engine(g_link, orbit::platform::clock_port(), g_usb, g_storage);
orbit::UiWorker g_ui(g_link, orbit::platform::clock_port(), g_gpio);
}  // namespace

void setup() { g_engine.setup(); }
void loop() { g_engine.poll(); }

void setup1() { g_ui.setup(); }
void loop1() { g_ui.poll(); }
