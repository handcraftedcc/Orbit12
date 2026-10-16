#pragma once

// T12 screen renderer. Core 1 only: formats the bounded UiModel snapshot into a
// 1024-byte SH1106 frame. Host-compilable: no Arduino includes, no heap, no
// STL containers. The adapter (platform/macropad) presents finished frames.
//
// Source oracle: circuitpython-source/src/apps/Orion/core/ui.py (layout, tile
// grids, palettes, cursor rules) and Orion.py (splash, sleep, wake). The
// expected frames in tests/native/fixtures/t12_render_traces.inc come from
// tools/arduino/oracle/t12_render_oracle.py, which runs ui.py itself.

#include <cstddef>
#include <cstdint>

#include "app/ui_model.hpp"
#include "domain/time.hpp"

namespace orbit::render {

constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr std::size_t kFrameBytes = 1024;      // 128 x 64 / 8
constexpr std::uint32_t kSplashMs = 1500;      // code.py: time.sleep(1.5)
constexpr std::uint32_t kSleepIdleMs = 60000;  // Orion.py SCREEN_SLEEP_TIME
constexpr std::uint32_t kFrameIntervalMs = 33; // ceiling of 30 Hz on the UI path

// SH1106 page order: byte index x + (y / 8) * 128, bit (y % 8), bit 0 is the top
// pixel of each page. This is the buffer Adafruit_SH110X::display() transmits.
struct Frame {
  std::uint8_t bytes[kFrameBytes]{};
  void clear() noexcept;
  bool pixel(int x, int y) const noexcept;
};

// Largest sheet tile the column blitter accepts (ChainNav_12x5.1 is 12 wide).
constexpr int kMaxTileWidth = 12;
constexpr int kMaxTileHeight = 8;

// View of a 1 bpp BMP tile sheet stored in const memory. Tile n sits at column
// n % columns and row n / columns; ink is bit value 1 (palette entries are not
// used, matching the Python TileGrid palettes).
class Sheet {
 public:
  // constexpr so the screen assets can be decoded at compile time (see
  // renderer.cpp, flash column tables). Rejects anything the blitter cannot hold.
  constexpr bool open(const std::uint8_t* bmp, std::size_t size, int tile_width, int tile_height) noexcept {
    bmp_ = nullptr;
    if (bmp == nullptr || size < 62 || tile_width <= 0 || tile_height <= 0) return false;
    if (tile_width > kMaxTileWidth || tile_height > kMaxTileHeight) return false;
    if (bmp[0] != 'B' || bmp[1] != 'M') return false;
    auto u32 = [bmp](std::size_t at) {
      return static_cast<std::uint32_t>(bmp[at]) | (static_cast<std::uint32_t>(bmp[at + 1]) << 8) |
             (static_cast<std::uint32_t>(bmp[at + 2]) << 16) | (static_cast<std::uint32_t>(bmp[at + 3]) << 24);
    };
    const auto offset = u32(10);
    const auto width = static_cast<std::int32_t>(u32(18));
    const auto height = static_cast<std::int32_t>(u32(22));
    const auto bits = static_cast<std::uint16_t>(bmp[28] | (bmp[29] << 8));
    if (bits != 1 || width <= 0 || height == 0 || offset >= size) return false;
    const int abs_height = height < 0 ? -height : height;
    const std::size_t stride = ((static_cast<std::size_t>(width) + 31U) / 32U) * 4U;
    if (static_cast<std::size_t>(abs_height) * stride + offset > size) return false;
    if (width % tile_width != 0 || abs_height % tile_height != 0) return false;

    bmp_ = bmp;
    size_ = size;
    data_offset_ = offset;
    stride_ = stride;
    width_ = width;
    height_ = abs_height;
    tile_width_ = tile_width;
    tile_height_ = tile_height;
    columns_ = width / tile_width;
    rows_ = abs_height / tile_height;
    top_down_ = height < 0;
    return true;
  }
  constexpr bool valid() const noexcept { return bmp_ != nullptr; }
  constexpr int tile_width() const noexcept { return tile_width_; }
  constexpr int tile_height() const noexcept { return tile_height_; }
  constexpr int columns() const noexcept { return columns_; }
  constexpr int tiles() const noexcept { return columns_ * rows_; }
  // Tile-local pixel. Out-of-range requests read as 0 (never past the data).
  bool ink_at(int tile, int x, int y) const noexcept;
  // Whole tile column at once: columns[tx] bit ty is ink_at(tile, tx, ty). The
  // array must hold tile_width() entries. Invalid or out-of-range tiles read 0.
  // A table attached with attach_columns() answers for the tiles it covers.
  constexpr void tile_columns(int tile, std::uint32_t* columns) const noexcept {
    for (int tx = 0; tx < tile_width_; ++tx) columns[tx] = 0;
    if (!valid() || tile < 0 || tile >= tiles()) return;
    if (cache_ != nullptr && tile < cache_tiles_) {
      for (int tx = 0; tx < tile_width_; ++tx) {
        columns[tx] = cache_[static_cast<std::size_t>(tile) * static_cast<std::size_t>(tile_width_) +
                             static_cast<std::size_t>(tx)];
      }
      return;
    }
    const int origin_x = (tile % columns_) * tile_width_;
    const int origin_y = (tile / columns_) * tile_height_;
    for (int ty = 0; ty < tile_height_; ++ty) {
      const int py = origin_y + ty;
      const int row = top_down_ ? py : height_ - 1 - py;
      const std::size_t row_start = data_offset_ + static_cast<std::size_t>(row) * stride_;
      for (int tx = 0; tx < tile_width_; ++tx) {
        const auto px = static_cast<std::size_t>(origin_x + tx);
        const std::size_t byte_index = row_start + px / 8U;
        if (byte_index >= size_) continue;
        if (((bmp_[byte_index] >> (7 - px % 8U)) & 1U) != 0) columns[tx] |= 1U << ty;
      }
    }
  }
  // Column table in flash, tile-major (tile * tile_width + tx), covering the
  // first `tiles` tiles. Built from the same bytes at compile time.
  constexpr void attach_columns(const std::uint8_t* table, int tiles) noexcept {
    cache_ = table;
    cache_tiles_ = tiles;
  }

 private:
  const std::uint8_t* bmp_ = nullptr;
  std::size_t size_ = 0;
  std::size_t data_offset_ = 0;
  std::size_t stride_ = 0;
  int width_ = 0;
  int height_ = 0;
  int tile_width_ = 0;
  int tile_height_ = 0;
  int columns_ = 0;
  int rows_ = 0;
  bool top_down_ = false;
  const std::uint8_t* cache_ = nullptr;  // flash column table, or null
  int cache_tiles_ = 0;
};

// The four sheets used by the screens (T03 const assets).
struct Assets {
  Sheet font;          // Font_6x6: 6x6 glyphs, FONTCHARS order
  Sheet state_icons;   // StateIndicators_7x5
  Sheet chain_nav;     // ChainNav_12x5.1: 12x6 cursor tiles
  Sheet chain_preview; // ChainPreview_3x3
  bool load() noexcept;
  bool valid() const noexcept;
};

// Tile index for a character (ui.py tile_for_char): position in FONTCHARS, 0
// for anything not in the table.
int font_tile_for(char c) noexcept;

// Composes the full screen from the snapshot. Pure function of its inputs.
void render_screen(const UiModel& model, const Assets& assets, Frame& frame) noexcept;

// Diagnostic: pixel-column writes made by the most recent render_screen call on
// this core (reset at the start of each call). Test and budget checks only.
std::uint32_t column_writes_last_frame() noexcept;

// Phase of the screen: launch image, normal UI, or display asleep. The adapter
// turns phase edges into display power commands; sleeping never touches the
// NeoPixels.
enum class ScreenPhase : std::uint8_t { Splash, Active, Asleep };

class ScreenPower {
 public:
  void start(std::uint32_t now_ms) noexcept {
    started_ms_ = now_ms;
    last_activity_ms_ = now_ms;
    phase_ = ScreenPhase::Splash;
  }
  // Any key, encoder switch or encoder turn. Returns true when the screen woke.
  bool activity(std::uint32_t now_ms) noexcept {
    last_activity_ms_ = now_ms;
    if (phase_ != ScreenPhase::Asleep) return false;
    phase_ = ScreenPhase::Active;
    return true;
  }
  // Returns true when the screen went to sleep on this call. Sleeping requires
  // strictly more than kSleepIdleMs since the last activity.
  bool tick(std::uint32_t now_ms) noexcept {
    if (phase_ == ScreenPhase::Splash) {
      if (deadline_reached(now_ms, started_ms_ + kSplashMs)) phase_ = ScreenPhase::Active;
      return false;
    }
    if (phase_ == ScreenPhase::Active && elapsed(now_ms, last_activity_ms_) > kSleepIdleMs) {
      phase_ = ScreenPhase::Asleep;
      return true;
    }
    return false;
  }
  ScreenPhase phase() const noexcept { return phase_; }

 private:
  std::uint32_t started_ms_ = 0;
  std::uint32_t last_activity_ms_ = 0;
  ScreenPhase phase_ = ScreenPhase::Splash;
};

}  // namespace orbit::render
