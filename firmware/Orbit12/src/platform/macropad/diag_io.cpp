#include "platform/macropad/diag_io.hpp"

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#include <cstring>

#include "platform/macropad/launchscreen_bitmap.h"

extern "C" {
extern char __StackBottom, __StackTop, __StackOneBottom, __StackOneTop;
}

namespace orbit {
namespace platform {
namespace {
Adafruit_SH1106G g_display(128, 64, &SPI1, OLED_DC, OLED_RST, OLED_CS);
Adafruit_NeoPixel g_pixels(NUM_NEOPIXEL, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
constexpr std::uint32_t kPaint = 0xA5A5A5A5U;
}  // namespace

void display_begin() noexcept {
  g_display.begin(0, true);
  g_display.setTextColor(SH110X_WHITE);
  g_display.setTextSize(1);
  g_display.clearDisplay();
  g_display.display();
}

void display_splash() noexcept {
  g_display.clearDisplay();
  // Original BMP palette: index 0 white, index 1 black (verified in the T00 smoke).
  g_display.drawBitmap(0, 0, kLaunchScreenBitmap, 128, 64, SH110X_BLACK, SH110X_WHITE);
  g_display.display();
}

void display_present(const TextScreen& screen) noexcept {
  g_display.clearDisplay();
  g_display.setTextColor(SH110X_WHITE);
  g_display.setTextSize(1);
  for (std::size_t row = 0; row < kScreenRows; ++row) {
    g_display.setCursor(0, static_cast<int16_t>(row * 8U));
    g_display.print(screen.line[row]);
  }
  g_display.display();
}

bool display_frame(const std::uint8_t* frame, std::size_t length) noexcept {
  constexpr std::size_t kFrameBytes = 128U * 64U / 8U;
  if (frame == nullptr || length != kFrameBytes) return false;
  std::uint8_t* shown = g_display.getBuffer();
  if (std::memcmp(shown, frame, kFrameBytes) == 0) return false;
  // display() sends only the dirty window and resets it afterwards; clearDisplay()
  // restores the full window (and zeroes the buffer, which the copy overwrites).
  g_display.clearDisplay();
  std::memcpy(shown, frame, kFrameBytes);
  g_display.display();  // one 1 KB transfer at the library default of 8 MHz, core 1 only
  return true;
}

void display_power(bool on) noexcept {
  g_display.oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
}

void pixels_begin() noexcept {
  g_pixels.begin();
  g_pixels.clear();
  g_pixels.show();
}

void pixels_show(const RgbColor* colors, std::size_t count) noexcept {
  for (std::size_t i = 0; i < count && i < NUM_NEOPIXEL; ++i)
    g_pixels.setPixelColor(static_cast<uint16_t>(i), colors[i].red, colors[i].green, colors[i].blue);
  g_pixels.show();
}

void serial_begin() noexcept { Serial.begin(115200); }
int serial_read() noexcept { return Serial.available() > 0 ? Serial.read() : -1; }
std::size_t serial_writable() noexcept {
  const int n = Serial.availableForWrite();
  return n > 0 ? static_cast<std::size_t>(n) : 0U;
}
std::size_t serial_write(const char* data, std::size_t length) noexcept {
  // Callers never exceed serial_writable(), so the library's yield-and-retry
  // loop cannot spin; it returns 0 immediately when no host holds the port open.
  return Serial.write(reinterpret_cast<const uint8_t*>(data), length);
}
bool serial_connected() noexcept { return static_cast<bool>(Serial); }

// Linker reserves 2 KB per core (core 0: 0x20041800..0x20042000, core 1:
// 0x20040800..0x20041000). The 2 KB directly below each is unused scratch RAM
// (.scratch_x/.scratch_y are empty in this build), so the watermark covers
// that headroom too: used > nominal means the nominal reservation was exceeded.
char* stack_bottom(std::uint8_t core) noexcept { return core == 0 ? &__StackOneTop : &__StackOneBottom - 2048; }
char* stack_top(std::uint8_t core) noexcept { return core == 0 ? &__StackTop : &__StackOneTop; }

void stack_paint(std::uint8_t core) noexcept {
  char* const bottom = stack_bottom(core);
  char* const top = stack_top(core);
  char* const here = static_cast<char*>(__builtin_frame_address(0));
  if (here <= bottom || here >= top) return;  // not running in this region: report unknown
  auto* word = reinterpret_cast<std::uint32_t*>(bottom);
  auto* const end = reinterpret_cast<std::uint32_t*>(here - 64);  // keep the live frames intact
  while (word < end) *word++ = kPaint;
}

StackReport stack_report(std::uint8_t core) noexcept {
  StackReport r;
  char* const bottom = stack_bottom(core);
  char* const top = stack_top(core);
  r.nominal = core == 0 ? static_cast<std::uint32_t>(&__StackTop - &__StackBottom)
                        : static_cast<std::uint32_t>(&__StackOneTop - &__StackOneBottom);
  char local = 0;
  r.sp = reinterpret_cast<std::uint32_t>(&local);
  r.bottom = reinterpret_cast<std::uint32_t>(bottom);
  r.top = reinterpret_cast<std::uint32_t>(top);
  r.capacity = static_cast<std::uint32_t>(top - bottom);
  r.valid = r.sp > r.bottom && r.sp < r.top;
  const auto* word = reinterpret_cast<const std::uint32_t*>(bottom);
  const auto* const end = reinterpret_cast<const std::uint32_t*>(top);
  while (word < end && *word == kPaint) ++word;
  r.used = static_cast<std::uint32_t>(reinterpret_cast<const char*>(end) - reinterpret_cast<const char*>(word));
  return r;
}

HeapReport heap_report() noexcept {
  HeapReport h;
  h.total = rp2040.getTotalHeap();
  h.used = rp2040.getUsedHeap();
  h.free = rp2040.getFreeHeap();
  return h;
}

}  // namespace platform
}  // namespace orbit
