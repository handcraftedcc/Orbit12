// T12c renderer sweep: every FONTCHARS glyph is placed in text cells on each
// palette, scale and row phase the screens use, and the lit pixels are checked
// against the sheet bits read through Sheet::ink_at. The palette rules are the
// ones in ui.py (header inverted on the white bar, chain opaque, parameters
// transparent on blank cells). Also checks the per-frame column-write budget.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/renderer.hpp"
#include "platform/macropad/orion_assets.hpp"

static int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

using namespace orbit;
using namespace orbit::render;

namespace {

// ui.py FONTCHARS, copied as a fixture (69 glyphs, two apostrophes, one backslash).
constexpr char kGlyphs[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:''()[]{}<>+-=*/\\_%&@#$^|~`";
constexpr int kGlyphCount = 69;
static_assert(sizeof(kGlyphs) - 1 == kGlyphCount, "FONTCHARS fixture size");

// Frame-column budget for any screen. Sections are drawn exclusively, so the
// worst screen is the chain page: chain text 10 cells x 6 columns x 2 (scale 2)
// = 120, chain cursors 20 tiles x 12 = 240, header 224 = 584. The parameter
// page is header 224 + parameter rows 330 = 554. Budget 600.
constexpr std::uint32_t kColumnWriteBudget = 600;

Assets g_assets;
std::uint32_t g_max_writes = 0;

// Tile index of a glyph as ui.py numbers it (position in FONTCHARS, first match).
int glyph_index(char c) {
  for (int i = 0; i < kGlyphCount; ++i) {
    if (kGlyphs[i] == c) return i;
  }
  return 0;
}

// `cells` glyphs starting at FONTCHARS position `start`, wrapping around the table.
void make_label(char* out, int cells, int start) {
  for (int c = 0; c < cells; ++c) out[c] = kGlyphs[(start + c) % kGlyphCount];
  out[cells] = '\0';
}

void note_frame() {
  const std::uint32_t writes = column_writes_last_frame();
  CHECK(writes > 0);
  CHECK(writes <= kColumnWriteBudget);
  if (writes > g_max_writes) g_max_writes = writes;
}

// Checks one 6x6 glyph cell drawn with `scale` at (x, y). Ink pixels must be
// `ink`, blank pixels `blank`; -1 means the pixel stays at its background value,
// which is black on the cleared parameter frame.
void expect_cell(const Frame& frame, int tile, int x, int y, int scale, int ink, int blank) {
  const Sheet& font = g_assets.font;
  for (int ty = 0; ty < font.tile_height(); ++ty) {
    for (int tx = 0; tx < font.tile_width(); ++tx) {
      const int value = font.ink_at(tile, tx, ty) ? ink : blank;
      for (int sy = 0; sy < scale; ++sy) {
        for (int sx = 0; sx < scale; ++sx) {
          const bool lit = frame.pixel(x + tx * scale + sx, y + ty * scale + sy);
          if (value < 0) {
            CHECK(!lit);
          } else {
            CHECK(lit == (value == 1));
          }
        }
      }
    }
  }
}

void font_mapping_is_complete() {
  CHECK(g_assets.load());
  for (int i = 0; i < kGlyphCount; ++i) {
    CHECK(font_tile_for(kGlyphs[i]) == glyph_index(kGlyphs[i]));
  }
  CHECK(font_tile_for('a') == 0);
  CHECK(font_tile_for(static_cast<char>(0xE9)) == 0);
}

// Parameter rows at scale 1 (transparent on blank cells). The highlighted row is
// not drawn here, so every label row sits at 15 + 8 * row (+ 5 below the
// highlight). Rows 0 and 2 cover two different phases of the 8-row page.
void sweep_parameter_labels_scale1() {
  for (int start = 0; start < kGlyphCount; ++start) {
    char row0[7];
    char row2[7];
    make_label(row0, 6, start);
    make_label(row2, 6, start + 3);
    UiModel model{};
    model.section = UiSection::ParmSelection;
    model.active_chain = 2;
    model.active_parm = 1;
    model.highlight_row = 1;
    model.scroll_offset = 0;
    model.parm_count = 4;
    model.rows[0].label = row0;
    model.rows[0].present = true;
    model.rows[2].label = row2;
    model.rows[2].present = true;
    Frame frame;
    render_screen(model, g_assets, frame);
    note_frame();
    for (int c = 0; c < 6; ++c) {
      expect_cell(frame, glyph_index(row0[c]), 4 + 6 * c, 15, 1, 1, -1);
      expect_cell(frame, glyph_index(row2[c]), 4 + 6 * c, 36, 1, 1, -1);
    }
  }
}

// The highlighted parameter row is scale 2 (12-pixel cells). Its value column
// starts at x 66, so cells 0..4 (x 4..63) are free of value pixels. Every
// highlight position is covered, which moves the scale-2 row over the page.
void sweep_parameter_highlight_scale2() {
  for (int highlight = 0; highlight < 4; ++highlight) {
    for (int start = 0; start < kGlyphCount; start += 5) {
      char labels[4][7];
      UiModel model{};
      model.section = UiSection::ParmSelection;
      model.active_chain = 2;
      model.active_parm = static_cast<std::int8_t>(highlight);
      model.highlight_row = static_cast<std::int8_t>(highlight);
      model.scroll_offset = 0;
      model.parm_count = 4;
      for (int i = 0; i < 4; ++i) {
        make_label(labels[i], 6, start + 7 * i);
        model.rows[i].label = labels[i];
        model.rows[i].present = true;
      }
      Frame frame;
      render_screen(model, g_assets, frame);
      note_frame();
      for (int i = 0; i < 4; ++i) {
        if (i == highlight) {
          const int y = 15 + 8 * i;
          for (int c = 0; c < 5; ++c) expect_cell(frame, glyph_index(labels[i][c]), 4 + 12 * c, y, 2, 1, -1);
        } else {
          const int y = 15 + 8 * i + (i > highlight ? 5 : 0);
          for (int c = 0; c < 6; ++c) expect_cell(frame, glyph_index(labels[i][c]), 4 + 6 * c, y, 1, 1, -1);
        }
      }
    }
  }
}

// Module selector: opaque chain palette at scale 2 over the full 10 cells.
// Cell 0 is '<', cell 1 is blank, cells 2..8 are the label, cell 9 is '>'.
void sweep_module_selector() {
  for (int start = 0; start < kGlyphCount; ++start) {
    char label[8];
    make_label(label, 7, start);
    UiModel model{};
    model.section = UiSection::ModuleSelection;
    model.selector_label = label;
    Frame frame;
    render_screen(model, g_assets, frame);
    note_frame();
    expect_cell(frame, glyph_index('<'), 4, 27, 2, 1, 0);
    expect_cell(frame, 0, 4 + 12, 27, 2, 1, 0);
    for (int c = 0; c < 7; ++c) expect_cell(frame, glyph_index(label[c]), 4 + 12 * (c + 2), 27, 2, 1, 0);
    expect_cell(frame, glyph_index('>'), 4 + 12 * 9, 27, 2, 1, 0);
  }
}

// Module label in the header: inverted ink on the white bar (ink black, blank
// white), scale 1 at (26, 3) over six cells.
void sweep_header_label() {
  for (int start = 0; start < kGlyphCount; ++start) {
    char label[7];
    make_label(label, 6, start);
    UiModel model{};
    model.section = UiSection::Chain;
    model.active_chain = 0;
    model.slots[0].label = label;
    Frame frame;
    render_screen(model, g_assets, frame);
    note_frame();
    for (int c = 0; c < 6; ++c) expect_cell(frame, glyph_index(label[c]), 26 + 6 * c, 3, 1, 0, 1);
  }
}


// The flash column tables attached in Assets::load must equal the bits an
// uncached Sheet reads for every tile. Out-of-range tiles read all-zero columns.
void cached_columns_match_bits() {
  Sheet plain_font;
  Sheet plain_chain;
  CHECK(plain_font.open(assets::bmp_font_6x6, assets::bmp_font_6x6_size, 6, 6));
  CHECK(plain_chain.open(assets::bmp_chainnav_12x5_1, assets::bmp_chainnav_12x5_1_size, 12, 6));
  const struct { const Sheet* cached; const Sheet* plain; } pairs[] = {
      {&g_assets.font, &plain_font}, {&g_assets.chain_nav, &plain_chain}};
  for (const auto& pair : pairs) {
    CHECK(pair.cached->tiles() == pair.plain->tiles());
    for (int tile = 0; tile < pair.plain->tiles(); ++tile) {
      std::uint32_t got[12] = {};
      std::uint32_t want[12] = {};
      pair.cached->tile_columns(tile, got);
      pair.plain->tile_columns(tile, want);
      for (int tx = 0; tx < pair.plain->tile_width(); ++tx) CHECK(got[tx] == want[tx]);
    }
    std::uint32_t none[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    pair.cached->tile_columns(pair.plain->tiles(), none);
    for (int tx = 0; tx < pair.plain->tile_width(); ++tx) CHECK(none[tx] == 0);
  }
}

}  // namespace

int main() {
  font_mapping_is_complete();
  cached_columns_match_bits();
  sweep_parameter_labels_scale1();
  sweep_parameter_highlight_scale2();
  sweep_module_selector();
  sweep_header_label();
  if (g_failures != 0) {
    std::fprintf(stderr, "t12c_renderer_sweep: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("t12c_renderer_sweep: %d glyphs over label, selector and header cells passed; max column writes %u of %u\n",
              kGlyphCount, static_cast<unsigned>(g_max_writes), static_cast<unsigned>(kColumnWriteBudget));
  return 0;
}
