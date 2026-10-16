// Link-time guard: TinyUSB is serviced only from core 0 (contracts.md: core 0 owns USB).
//
// Arduino-pico's yield() (cores/rp2040/delay.cpp) calls TinyUSB_Device_Task() and
// TinyUSB_Device_FlushCDC(). Adafruit_SH110X::display(), Adafruit_GFX and Adafruit_SPITFT call
// yield(), so the core-1 render path would otherwise run tud_task on core 1 (callbacks,
// unsynchronized FIFO access with core-0 MIDI writes). The wrappers below are linked with
// -Wl,--wrap=<symbol> (see tools/arduino/build-firmware.sh). --wrap only redirects references
// that cross object files, so every symbol listed here is one that the core/library calls from
// another translation unit:
//   TinyUSB_Device_Task      <- yield()           (rp2040 port, Adafruit_TinyUSB_rp2040.cpp)
//   TinyUSB_Device_FlushCDC  <- yield()           (Adafruit_TinyUSB_API.cpp)
//   tud_task_ext             <- inline tud_task() (usbd.c; every caller in other objects)
//   tud_cdc_n_write_flush    <- FlushCDC and inline tud_cdc_write_flush() (cdc_device.c)
// On core 1 each wrapper returns immediately, so nothing touches TinyUSB state from core 1.
// Core 0 calls pass through unchanged. Platform layer only; the domain never sees this file.
#include <Arduino.h>
#include <pico/platform.h>

extern "C" {
void __real_TinyUSB_Device_Task(void);
void __real_TinyUSB_Device_FlushCDC(void);
void __real_tud_task_ext(uint32_t timeout_ms, bool in_isr);
uint32_t __real_tud_cdc_n_write_flush(uint8_t itf);

void __wrap_TinyUSB_Device_Task(void) {
  if (get_core_num() != 0) return;
  __real_TinyUSB_Device_Task();
}

void __wrap_TinyUSB_Device_FlushCDC(void) {
  if (get_core_num() != 0) return;
  __real_TinyUSB_Device_FlushCDC();
}

void __wrap_tud_task_ext(uint32_t timeout_ms, bool in_isr) {
  if (get_core_num() != 0) return;
  __real_tud_task_ext(timeout_ms, in_isr);
}

uint32_t __wrap_tud_cdc_n_write_flush(uint8_t itf) {
  if (get_core_num() != 0) return 0;
  return __real_tud_cdc_n_write_flush(itf);
}
}  // extern "C"
