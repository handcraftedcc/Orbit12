#pragma once
// Arduino-free declarations for the headless-slice peripherals. Each function
// group is owned by exactly one core (see comments); definitions live in
// diag_io.cpp, the only file that includes Arduino/Adafruit headers.
#include <cstddef>
#include <cstdint>

#include "../../domain/types.hpp"

namespace orbit {
namespace platform {

constexpr std::size_t kScreenColumns = 21;  // 128 px / 6 px glyph
constexpr std::size_t kScreenRows = 8;      // 64 px / 8 px line
struct TextScreen {
  char line[kScreenRows][kScreenColumns + 1]{};
};

// ---- Core 1 only: SH1106 over SPI1 and the NeoPixel strip ----
void display_begin() noexcept;                      // initialization allocation (framebuffer)
void display_splash() noexcept;                     // original launch bitmap
void display_present(const TextScreen& screen) noexcept;
// Core 1: presents a 1024-byte SH1106 page buffer (x + (y/8)*128, bit y%8). The
// library buffer doubles as the last-sent copy: nothing is transferred unless
// the frame differs. Returns true when a full transfer ran. Length must be 1024.
bool display_frame(const std::uint8_t* frame, std::size_t length) noexcept;
void display_power(bool on) noexcept;
void pixels_begin() noexcept;
void pixels_show(const RgbColor* colors, std::size_t count) noexcept;

// ---- Core 0 only: USB CDC maintenance serial (read-only dump channel) ----
void serial_begin() noexcept;
int serial_read() noexcept;                         // -1 when empty
std::size_t serial_writable() noexcept;             // bytes accepted now without waiting
std::size_t serial_write(const char* data, std::size_t length) noexcept;
bool serial_connected() noexcept;

// ---- Stack watermark and heap (call from the owning core) ----
struct StackReport {
  std::uint32_t capacity = 0;  // measured region (see diag_io.cpp), 0: unknown
  std::uint32_t nominal = 0;   // the 2 KB the linker reserves for this core
  std::uint32_t used = 0;      // deepest painted-pattern overwrite
  std::uint32_t sp = 0;        // address of a local at report time
  std::uint32_t bottom = 0, top = 0;
  bool valid = false;
};
void stack_paint(std::uint8_t core) noexcept;       // first statement of setup()/setup1()
StackReport stack_report(std::uint8_t core) noexcept;
struct HeapReport { std::uint32_t total = 0, used = 0, free = 0; };
HeapReport heap_report() noexcept;                  // core 0 only (cheap, bounded)

}  // namespace platform
}  // namespace orbit
