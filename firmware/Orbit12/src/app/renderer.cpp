#include "app/renderer.hpp"

#include "domain/music.hpp"
#include "platform/macropad/orion_assets.hpp"

// Coordinates and ink follow ui.py exactly (see the comments on each draw
// function). Text is drawn cell by cell, like displayio TileGrid cells: blank
// cells are tile 0, which is transparent only on the parameter palette.

namespace orbit::render {
namespace {

// ui.py FONTCHARS (69 entries, with two apostrophes and one backslash).
constexpr char kFontChars[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:''()[]{}<>+-=*/\\_%&@#$^|~`";
constexpr char kChainItems[] = "SI123456O*";  // ui.py CHAINITEMS

// Tint = (zero, one) palette: 0 black, 1 white, -1 transparent.
struct Tint {
  int zero;
  int one;
};
constexpr Tint kHeaderText{1, 0};   // header/footer: [0] white, [1] black
constexpr Tint kChainText{0, 1};    // chain and module selector: [0] black, [1] white
constexpr Tint kParmText{-1, 1};    // parameter section: index 0 transparent

constexpr int kCellWidth = 6;       // FONTBITMAP_DIMENSIONS
constexpr int kCellHeight = 6;
constexpr int kChainCellStep = 12;  // ChainNav tiles are 12 px wide
constexpr int kParmSectionTop = 15; // ParameterSection rows start at y=15
constexpr int kParmRowStep = 8;
constexpr int kParmRowGap = 5;      // rows after the highlighted row shift down
constexpr int kParmLabelX = 4;
constexpr int kParmValueX = 91;     // scale 1 value column (group space)
constexpr int kParmValueHighlightX = 31;
constexpr int kParmValueCells = 5;  // PARMVALUECHARS
constexpr int kParmLabelCells = 6;  // PARMLABELCHARS
constexpr int kPreviewTile = 3;
constexpr int kChainCount = 10;     // ui.py: 10 chain elements in the preview

constexpr int kChainSelectTop = 3;  // ChainNav tile indices
constexpr int kChainSelectBottom = 4;
constexpr int kChainSwapTile = 2;
constexpr int kChainEmptyBottom = 1;
constexpr int kStateTransportRunning = 0;
constexpr int kStateTransportStopped = 1;
constexpr int kStateTransportExternal = 2;
constexpr int kStateNotes = 3;
constexpr int kStateNav = 4;

// Rendered tile height (rows times scale) must fit one 32-bit column write.
constexpr int kMaxColumnRows = 24;

// Font character -> tile index as a flash table (128 entries, 0 for anything
// not in FONTCHARS, including lowercase and non-ASCII bytes).
struct FontIndex {
  std::uint8_t tile[128];
  constexpr FontIndex() : tile{} {
    // Backwards, so the first occurrence wins (FONTCHARS repeats the apostrophe).
    for (std::size_t i = sizeof(kFontChars) - 1; i-- > 0;) {
      tile[static_cast<std::uint8_t>(kFontChars[i])] = static_cast<std::uint8_t>(i);
    }
  }
};
constexpr FontIndex kFontIndex{};

// Bounded strlen. Labels are short; anything longer is cut at the limit.
std::size_t bounded_length(const char* text, std::size_t limit) noexcept {
  if (text == nullptr) return 0;
  std::size_t n = 0;
  while (n < limit && text[n] != '\0') ++n;
  return n;
}

// Column writes in the current frame (see column_writes_last_frame()).
std::uint32_t g_column_writes = 0;

// Writes one pixel column, rows y0 .. y0 + rows - 1 (rows <= kMaxColumnRows).
// Bit i of `set` makes row y0 + i white, bit i of `clear` makes it black; rows
// in neither mask keep their value. Clipped to the screen. Touches at most
// ceil((shift + rows) / 8) page bytes, which is one or two for the sheet tiles.
void write_column(Frame& frame, int x, int y0, int rows, std::uint32_t set, std::uint32_t clear) noexcept {
  ++g_column_writes;
  if (x < 0 || x >= kWidth || rows <= 0) return;
  if (y0 < 0) {
    if (-y0 >= rows) return;
    set >>= -y0;
    clear >>= -y0;
    rows += y0;
    y0 = 0;
  }
  if (y0 >= kHeight) return;
  if (y0 + rows > kHeight) rows = kHeight - y0;
  const std::uint32_t span = (1U << rows) - 1U;
  const int shift = y0 & 7;
  const std::uint32_t set_bits = (set & span) << shift;
  const std::uint32_t clear_bits = (clear & span) << shift;
  const int pages = (shift + rows + 7) / 8;
  std::uint8_t* byte = frame.bytes + x + (y0 >> 3) * kWidth;
  for (int page = 0; page < pages; ++page) {
    const auto on = static_cast<std::uint8_t>(set_bits >> (8 * page));
    const auto off = static_cast<std::uint8_t>(clear_bits >> (8 * page));
    *byte = static_cast<std::uint8_t>((*byte | on) & static_cast<std::uint8_t>(~off));
    byte += kWidth;
  }
}

// Splits one column of ink (bit i = row i of `rows`) into the set and clear
// masks for a tint: ink takes tint.one, other rows take tint.zero, and -1 keeps
// the pixel. Same rules as the per-pixel composition they replace.
void paint_masks(std::uint32_t ink, int rows, Tint tint, std::uint32_t& set, std::uint32_t& clear) noexcept {
  const std::uint32_t all = (1U << rows) - 1U;
  const std::uint32_t inked = ink & all;
  const std::uint32_t blank = all & ~inked;
  set = 0;
  clear = 0;
  if (tint.one >= 0) {
    if (tint.one == 1) {
      set |= inked;
    } else {
      clear |= inked;
    }
  }
  if (tint.zero >= 0) {
    if (tint.zero == 1) {
      set |= blank;
    } else {
      clear |= blank;
    }
  }
}

// Filled rectangle, clipped to the screen (Rectangle with palette index 0). Each
// page the rectangle touches gets one bit mask applied across the run of columns.
void fill(Frame& frame, int x, int y, int width, int height, bool on) noexcept {
  const int x0 = x < 0 ? 0 : x;
  const int x1 = x + width > kWidth ? kWidth : x + width;
  const int y0 = y < 0 ? 0 : y;
  const int y1 = y + height > kHeight ? kHeight : y + height;
  if (x0 >= x1 || y0 >= y1) return;
  for (int page = y0 / 8; page <= (y1 - 1) / 8; ++page) {
    const int top = (y0 > page * 8 ? y0 : page * 8) - page * 8;
    const int bottom = (y1 < page * 8 + 8 ? y1 : page * 8 + 8) - page * 8;
    const auto mask = static_cast<std::uint8_t>(((1U << bottom) - 1U) & ~((1U << top) - 1U));
    std::uint8_t* row = frame.bytes + page * kWidth;
    for (int xx = x0; xx < x1; ++xx) {
      row[xx] = on ? static_cast<std::uint8_t>(row[xx] | mask)
                   : static_cast<std::uint8_t>(row[xx] & static_cast<std::uint8_t>(~mask));
    }
  }
}

// One tile at scale (group scale applied to the tile origin and pixel size).
// Each source column is read once, expanded vertically by `scale`, then written
// `scale` times horizontally as one column of page bits.
void blit_tile(Frame& frame, const Sheet& sheet, int tile, int x, int y, int scale, Tint tint) noexcept {
  if (!sheet.valid() || scale < 1) return;
  const int width = sheet.tile_width();
  const int tile_rows = sheet.tile_height();
  const int rows = tile_rows * scale;
  if (width > kMaxTileWidth || tile_rows > kMaxTileHeight || rows > kMaxColumnRows) return;  // Assets::load rejects these sheets
  std::uint32_t columns[kMaxTileWidth];
  sheet.tile_columns(tile, columns);
  for (int tx = 0; tx < width; ++tx) {
    std::uint32_t expanded = columns[tx];
    if (scale > 1) {
      expanded = 0;
      for (int ty = 0; ty < tile_rows; ++ty) {
        if ((columns[tx] >> ty) & 1U) expanded |= ((1U << scale) - 1U) << (ty * scale);
      }
    }
    std::uint32_t set = 0;
    std::uint32_t clear = 0;
    paint_masks(expanded, rows, tint, set, clear);
    if ((set | clear) == 0) continue;
    for (int sx = 0; sx < scale; ++sx) write_column(frame, x + tx * scale + sx, y, rows, set, clear);
  }
}

// ui.py set_text without a range: `cells` cells starting at x. Left alignment
// keeps the first characters. Right alignment pads on the left; when the text
// is too long, the first `cells` characters are kept (offset clamps at 0).
void draw_text(Frame& frame, const Sheet& font, const char* text, int cells, int x, int y, int scale,
               Tint tint, bool right_aligned) noexcept {
  const std::size_t length = bounded_length(text, 64);
  const int len = static_cast<int>(length);
  int offset = right_aligned ? cells - len : 0;
  if (offset < 0) offset = 0;
  for (int i = 0; i < cells; ++i) {
    const int index = i - offset;
    const char c = (index >= 0 && index < len) ? text[index] : ' ';
    blit_tile(frame, font, font_tile_for(c), x + i * kCellWidth * scale, y, scale, tint);
  }
}

// Appends a signed decimal integer (no printf, no heap). Returns false on overflow.
bool append_int(char* out, std::size_t capacity, std::size_t& length, std::int32_t value) noexcept {
  char digits[12];
  std::size_t count = 0;
  std::uint32_t magnitude = value < 0 ? static_cast<std::uint32_t>(0U - static_cast<std::uint32_t>(value))
                                      : static_cast<std::uint32_t>(value);
  do {
    digits[count++] = static_cast<char>('0' + magnitude % 10U);
    magnitude /= 10U;
  } while (magnitude != 0 && count < sizeof(digits));
  if (value < 0) digits[count++] = '-';
  while (count > 0) {
    if (length + 1 >= capacity) return false;
    out[length++] = digits[--count];
  }
  out[length] = '\0';
  return true;
}

bool append_text(char* out, std::size_t capacity, std::size_t& length, const char* text) noexcept {
  if (text == nullptr) return true;
  for (std::size_t i = 0; text[i] != '\0'; ++i) {
    if (length + 1 >= capacity) return false;
    out[length++] = text[i];
  }
  out[length] = '\0';
  return true;
}

// ui.py HeaderFooter.update_header_key_info: custom text, or key + octave + scale.
void header_key_text(const UiModel& model, char* out, std::size_t capacity) noexcept {
  out[0] = '\0';
  if (model.key_custom_text != nullptr && model.key_custom_text[0] != '\0') {
    std::size_t length = 0;
    (void)append_text(out, capacity, length, model.key_custom_text);
    return;
  }
  std::size_t length = 0;
  const std::size_t key = model.key < music::kNotes.size() ? model.key : 0;
  const std::size_t scale = model.scale < music::kScaleNames.size() ? model.scale : 0;
  (void)append_text(out, capacity, length, music::kNotes[key]);
  (void)append_int(out, capacity, length, model.octave);
  (void)append_text(out, capacity, length, music::kScaleNames[scale]);
}

// ui.py Chain (group scale 2 at (4,27)) with ChainNav cursors at x=3 (step 12),
// top at y=17 and bottom at y=41. Chain tiles are opaque black/white.
void draw_chain(Frame& frame, const UiModel& model, const Assets& assets) noexcept {
  draw_text(frame, assets.font, kChainItems, 10, 4, 27, 2, kChainText, false);

  const int active = model.active_chain < kChainCount ? model.active_chain : 0;
  for (int i = 0; i < kChainCount; ++i) {
    int top = 0;
    int bottom = model.slots[i].empty ? kChainEmptyBottom : 0;
    if (i == active) {
      if (model.chain_mode == UiChainMode::Select) {
        top = kChainSelectTop;
        bottom = kChainSelectBottom;
      } else {
        top = kChainSwapTile;
        bottom = kChainSwapTile;
      }
    }
    blit_tile(frame, assets.chain_nav, top, 3 + kChainCellStep * i, 17, 1, kChainText);
    blit_tile(frame, assets.chain_nav, bottom, 3 + kChainCellStep * i, 41, 1, kChainText);
  }
}

// ui.py HeaderFooter: white bars (11 px) at top and bottom, chain id, preview
// strip, module label, key/scale text, scene number and state icons.
void draw_header_footer(Frame& frame, const UiModel& model, const Assets& assets) noexcept {
  fill(frame, 0, 0, kWidth, 11, true);
  fill(frame, 0, 53, kWidth, 11, true);

  const int active = model.active_chain < kChainCount ? model.active_chain : 0;
  const char letter[2] = {kChainItems[active], '\0'};
  draw_text(frame, assets.font, letter, 1, 4, 3, 1, kHeaderText, false);

  for (int i = 0; i < kChainCount; ++i) {
    int tile = 2;
    if (i == active) {
      tile = 3;
    } else if (model.slots[i].empty) {
      tile = 1;
    }
    blit_tile(frame, assets.chain_preview, tile, 10 + kPreviewTile * (i % 5),
              3 + kPreviewTile * (i / 5), 1, kHeaderText);
  }

  const char* module_label = model.slots[active].label != nullptr ? model.slots[active].label : "EMPTY";
  draw_text(frame, assets.font, module_label, 6, 26, 3, 1, kHeaderText, false);

  char key_text[24];
  header_key_text(model, key_text, sizeof(key_text));
  draw_text(frame, assets.font, key_text, 6, 89, 3, 1, kHeaderText, true);

  // "SCN NN" in 17 cells, left aligned at (4,56).
  char scene[8];
  const int scene_number = model.active_scene + 1;
  scene[0] = 'S';
  scene[1] = 'C';
  scene[2] = 'N';
  scene[3] = ' ';
  scene[4] = static_cast<char>('0' + (scene_number / 10) % 10);
  scene[5] = static_cast<char>('0' + scene_number % 10);
  scene[6] = '\0';
  draw_text(frame, assets.font, scene, 17, 4, 56, 1, kHeaderText, false);

  // Transport state (external overrides running), then notes/nav state.
  int transport = kStateTransportStopped;
  if (model.transport_mode == TransportMode::External) {
    transport = kStateTransportExternal;
  } else if (model.transport_running) {
    transport = kStateTransportRunning;
  }
  const int nav = model.nav_keys_state == 0 ? kStateNotes : kStateNav;
  blit_tile(frame, assets.state_icons, transport, 112, 56, 1, kHeaderText);
  blit_tile(frame, assets.state_icons, nav, 119, 56, 1, kHeaderText);
}

// ui.py ParameterSection: cursor first (drawn before the rows, as in the source
// group), then four rows. The highlighted row is scale 2 and its value moves to
// group x 31; rows after it shift down by 5.
void draw_parameters(Frame& frame, const UiModel& model, const Assets& assets) noexcept {
  // ParameterSection.rebuild_parm_section leaves the rows blank and draws no
  // cursor when the module has no parameters.
  if (model.parm_count == 0) return;
  const int active_parm = model.active_parm;
  // Layout uses the active parm clamped to the module range (negative selections
  // lay out row 0). The device-supplied highlight_row is not used for layout.
  int layout_parm = active_parm;
  if (layout_parm < 0) layout_parm = 0;
  if (layout_parm >= model.parm_count) layout_parm = model.parm_count - 1;
  const int highlight = layout_parm - model.scroll_offset;
  if (active_parm == -2) {
    fill(frame, 0, 3, 2, 5, false);  // chain marker, black on the header
  } else if (active_parm == -1) {
    const std::size_t label_length = bounded_length(model.slots[model.active_chain < kChainCount ? model.active_chain : 0].label, 16);
    const int x = 64 - (kParmLabelCells - static_cast<int>(label_length)) * kCellWidth;
    fill(frame, x, 3, 2, 5, false);  // module marker
  } else if (active_parm >= 0) {
    const int cursor_row = active_parm - model.scroll_offset;  // cursor_change_state, unclamped
    if (cursor_row >= 0 && cursor_row < kParmsPerPage) {
      const int x = model.section == UiSection::ParmEdit ? kWidth - 2 : 0;
      fill(frame, x, kParmSectionTop + cursor_row * kParmRowStep, 2, 10, true);
    }
  }

  for (int i = 0; i < kParmsPerPage; ++i) {
    const bool row_highlighted = i == highlight;
    const int scale = row_highlighted ? 2 : 1;
    const int y = kParmSectionTop + kParmRowStep * i + (i > highlight ? kParmRowGap : 0);
    const UiRow& row = model.rows[i];
    if (!row.present) continue;  // blank cells are transparent on this palette
    draw_text(frame, assets.font, row.label, kParmLabelCells, kParmLabelX, y, scale, kParmText, false);
    char value[32];
    (void)ui_format_value(row, value, sizeof(value));
    const int value_x = row_highlighted ? kParmValueHighlightX : kParmValueX;
    draw_text(frame, assets.font, value, kParmValueCells, kParmLabelX + scale * value_x, y, scale, kParmText,
              true);
  }
}

// ui.py ModuleSelector (group scale 2 at (4,27)): "< " + label in cells 2..8
// (left aligned, seven cells) + ">" in cell 9.
void draw_module_selector(Frame& frame, const UiModel& model, const Assets& assets) noexcept {
  char cells[11];
  for (char& c : cells) c = ' ';
  cells[0] = '<';
  cells[9] = '>';
  cells[10] = '\0';
  const char* label = model.selector_label != nullptr ? model.selector_label : "EMPTY";
  const std::size_t label_length = bounded_length(label, 7);
  for (std::size_t i = 0; i < label_length; ++i) cells[2 + i] = label[i];
  draw_text(frame, assets.font, cells, 10, 4, 27, 2, kChainText, false);
}

}  // namespace

void Frame::clear() noexcept {
  for (std::uint8_t& byte : bytes) byte = 0;
}

bool Frame::pixel(int x, int y) const noexcept {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return false;
  const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y >> 3) * kWidth;
  return ((bytes[index] >> (y & 7)) & 1U) != 0;
}

bool Sheet::ink_at(int tile, int x, int y) const noexcept {
  if (!valid() || tile < 0 || tile >= tiles() || x < 0 || y < 0 || x >= tile_width_ || y >= tile_height_) {
    return false;
  }
  const int px = (tile % columns_) * tile_width_ + x;
  const int py = (tile / columns_) * tile_height_ + y;
  const int row = top_down_ ? py : height_ - 1 - py;
  const std::size_t byte_index =
      data_offset_ + static_cast<std::size_t>(row) * stride_ + static_cast<std::size_t>(px) / 8U;
  if (byte_index >= size_) return false;
  return ((bmp_[byte_index] >> (7 - px % 8)) & 1U) != 0;
}

namespace {

// Column tables in flash for the two large sheets (font, chain nav): every tile's
// transposed bits, built at compile time from the same bytes Sheet::open reads.
// They use no RAM; the blitter reads one byte per tile column from them.
template <int Tiles, int TileWidth>
struct ColumnTable {
  std::uint8_t columns[Tiles * TileWidth];
};

template <int Tiles, int TileWidth>
constexpr ColumnTable<Tiles, TileWidth> build_column_table(const Sheet& sheet) noexcept {
  ColumnTable<Tiles, TileWidth> table{};
  for (int tile = 0; tile < Tiles; ++tile) {
    std::uint32_t columns[kMaxTileWidth] = {};
    sheet.tile_columns(tile, columns);
    for (int tx = 0; tx < TileWidth; ++tx) {
      table.columns[tile * TileWidth + tx] = static_cast<std::uint8_t>(columns[tx]);
    }
  }
  return table;
}

constexpr Sheet open_sheet(const std::uint8_t* bmp, std::size_t size, int tile_width, int tile_height) noexcept {
  Sheet sheet;
  (void)sheet.open(bmp, size, tile_width, tile_height);
  return sheet;
}

constexpr Sheet kFontSheet = open_sheet(assets::bmp_font_6x6, assets::bmp_font_6x6_size, 6, 6);
constexpr Sheet kChainNavSheet = open_sheet(assets::bmp_chainnav_12x5_1, assets::bmp_chainnav_12x5_1_size, 12, 6);
static_assert(kFontSheet.valid() && kChainNavSheet.valid(), "screen sheets must decode at compile time");
constexpr auto kFontColumns = build_column_table<kFontSheet.tiles(), 6>(kFontSheet);
constexpr auto kChainNavColumns = build_column_table<kChainNavSheet.tiles(), 12>(kChainNavSheet);

}  // namespace

bool Assets::load() noexcept {
  const bool ok = font.open(assets::bmp_font_6x6, assets::bmp_font_6x6_size, 6, 6) &&
                  state_icons.open(assets::bmp_stateindicators_7x5, assets::bmp_stateindicators_7x5_size, 7, 5) &&
                  chain_nav.open(assets::bmp_chainnav_12x5_1, assets::bmp_chainnav_12x5_1_size, 12, 6) &&
                  chain_preview.open(assets::bmp_chainpreview_3x3, assets::bmp_chainpreview_3x3_size, 3, 3);
  if (!ok) return false;
  font.attach_columns(kFontColumns.columns, kFontSheet.tiles());
  chain_nav.attach_columns(kChainNavColumns.columns, kChainNavSheet.tiles());
  return true;
}

bool Assets::valid() const noexcept {
  return font.valid() && state_icons.valid() && chain_nav.valid() && chain_preview.valid();
}

int font_tile_for(char c) noexcept {
  const auto code = static_cast<std::uint8_t>(c);
  return code < 128 ? kFontIndex.tile[code] : 0;
}

std::uint32_t column_writes_last_frame() noexcept { return g_column_writes; }

void render_screen(const UiModel& model, const Assets& assets, Frame& frame) noexcept {
  g_column_writes = 0;
  frame.clear();
  if (!assets.valid()) return;
  if (model.section == UiSection::Chain) draw_chain(frame, model, assets);
  draw_header_footer(frame, model, assets);
  if (model.section == UiSection::ParmSelection || model.section == UiSection::ParmEdit) {
    draw_parameters(frame, model, assets);
  }
  if (model.section == UiSection::ModuleSelection) draw_module_selector(frame, model, assets);
}

}  // namespace orbit::render
