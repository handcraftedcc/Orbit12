// T11b UI model helpers against tables generated from the real Python classes
// (State.move_active_parm_elem, ParameterSection paging, Parm.get_display_value).
#include <cstdio>
#include <cstring>

#include "app/ui_model.hpp"

static int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

using namespace orbit;

namespace {
#include "fixtures/t11b_model_tables.inc"

void move_parm() {
  for (const auto& c : kMoveCases)
    CHECK(move_parm_index(static_cast<std::int8_t>(c.active), c.delta, static_cast<std::uint8_t>(c.count), c.is_static != 0) == c.expect);
}

void paging() {
  ParmPaging p;
  p.parm_count = 4;  // ParameterSection initial value
  std::size_t i = 0;
  for (const auto& c : kPagingCases) {
    if (c.op == 'R') paging_rebuild(p, static_cast<std::int16_t>(c.active), static_cast<std::uint8_t>(c.count));
    else paging_update_selection(p, static_cast<std::int16_t>(c.active), static_cast<std::uint8_t>(c.count));
    const bool ok = p.parm_count == c.parm_count && p.scroll_offset == c.scroll && p.last_active_parm == c.last_active;
    if (!ok) std::fprintf(stderr, "paging case %zu (%c active=%d count=%d) diverged\n", i, c.op, c.active, c.count);
    CHECK(ok);
    if (!ok) return;
    ++i;
  }
}

void formatting() {
  static const char* const options[5] = {"NEXT", "ADD", "OUT+N", "OUT+S", "SKIP"};
  for (const auto& c : kFormatCases) {
    UiRow r;
    r.kind = c.kind; r.ivalue = c.ivalue; r.fvalue = c.fvalue; r.float_text = c.float_text != 0;
    r.present = true;
    if (c.option_count) { r.options = options; r.option_count = static_cast<std::uint8_t>(c.option_count); }
    char out[40];
    ui_format_value(r, out, sizeof(out));
    if (std::strcmp(out, c.expect) != 0) std::fprintf(stderr, "format kind=%d i=%d f=%g got '%s' want '%s'\n", static_cast<int>(c.kind), c.ivalue, static_cast<double>(c.fvalue), out, c.expect);
    CHECK(std::strcmp(out, c.expect) == 0);
  }
}

void colors_and_tables() {
  // Constants already fixed by the input modules (rainbowio.colorwheel 128 / 64).
  CHECK(kKeyColorBase.red == 0 && kKeyColorBase.green == 126 && kKeyColorBase.blue == 129);
  CHECK(kKeyColorRoot.red == 63 && kKeyColorRoot.green == 192 && kKeyColorRoot.blue == 0);
  CHECK(kNavNoteColors[1].green == 255 && kNavNoteColors[0].red == 255);   // start green, stop red
  CHECK(kNavParmColors[8].red == 129 && kNavParmColors[8].green == 126);   // ENTER colorwheel(42)
  const auto dim = combine_color_and_brightness(kKeyColorBase, 0.3);
  CHECK(dim.green == 37 && dim.blue == 38);                                // int() truncation
  CHECK(processor_selector_count() == 18 && input_selector_count() == 6);
  CHECK(std::strcmp(processor_selector(11).id, "pick") == 0);              // phraser hidden
  CHECK(std::strcmp(processor_selector(17).label, "WANDR") == 0);
  CHECK(std::strcmp(input_selector(5).label, "CHRDWLK") == 0);
}
}  // namespace

int main() {
  move_parm();
  paging();
  formatting();
  colors_and_tables();
  if (g_failures == 0) std::printf("t11b_ui_model: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
