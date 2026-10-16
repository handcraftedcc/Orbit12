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

// SCENE list (saved names in slot order, then [NEW]), its value text, the pattern X/O
// preview (music::kPatternBits, MSB first) and the name editor ring. Vectors are derived
// from the source table (music.py PATTERNS: bits and length per pattern id).
void scene_list_and_editor_text() {
  const auto mask = static_cast<std::uint16_t>((1U << 0) | (1U << 1) | (1U << 5));
  CHECK(scene_list_count(mask) == 4);
  CHECK(scene_list_slot(mask, 0) == 0);
  CHECK(scene_list_slot(mask, 2) == 5);
  CHECK(scene_list_slot(mask, 3) == kSceneListNewValue);
  CHECK(scene_list_slot(mask, 9) == kSceneListNewValue);  // beyond the list reads as [NEW]
  CHECK(scene_list_slot(mask, -1) == kSceneListNewValue);
  CHECK(scene_list_index(mask, 5) == 2);
  CHECK(scene_list_index(mask, 1) == 1);
  CHECK(scene_list_count(0) == 1);       // [NEW] only
  CHECK(scene_list_count(0xFFFF) == 17);
  for (std::uint8_t slot = 0; slot < 16; ++slot) {
    CHECK(scene_list_slot(0xFFFF, static_cast<std::int32_t>(scene_list_index(0xFFFF, slot))) == slot);
  }

  // SCENE value text is the entry's name; the renderer keeps the first five characters.
  UiRow row{};
  row.kind = RowKind::SceneSelect;
  row.present = true;
  char text[16];
  std::memcpy(row.text, "BRAVO", sizeof("BRAVO"));
  row.ivalue = 1;  (void)ui_format_value(row, text, sizeof text);
  CHECK(std::strcmp(text, "BRAVO") == 0);
  std::memcpy(row.text, "CHARLIE", sizeof("CHARLIE"));
  (void)ui_format_value(row, text, sizeof text);
  CHECK(std::strcmp(text, "CHARLIE") == 0);  // the full name; the renderer cuts it to the field
  row.ivalue = kSceneListNewValue; (void)ui_format_value(row, text, sizeof text);
  CHECK(std::strcmp(text, "NEW") == 0);

  char steps[kFooterTextChars + 1];
  CHECK(ui_pattern_steps(0, steps, sizeof steps) == 2 && std::strcmp(steps, "XX") == 0);
  CHECK(ui_pattern_steps(1, steps, sizeof steps) == 2 && std::strcmp(steps, "XO") == 0);
  CHECK(ui_pattern_steps(3, steps, sizeof steps) == 3 && std::strcmp(steps, "XOO") == 0);
  CHECK(ui_pattern_steps(16, steps, sizeof steps) == 6 && std::strcmp(steps, "XOXOOX") == 0);
  CHECK(ui_pattern_steps(22, steps, sizeof steps) == 8 && std::strcmp(steps, "XOXXXXOX") == 0);
  CHECK(ui_pattern_steps(24, steps, sizeof steps) == 8 && std::strcmp(steps, "XOXXOOXO") == 0);
  CHECK(ui_pattern_steps(25, steps, sizeof steps) == 0 && steps[0] == '\0');  // out of range
  char small[4];
  CHECK(ui_pattern_steps(16, small, sizeof small) == 0);  // capacity below 9: nothing written

  CHECK(std::strcmp(name_ring_label(0), "A") == 0);
  CHECK(std::strcmp(name_ring_label(25), "Z") == 0);
  CHECK(std::strcmp(name_ring_label(26), "0") == 0);
  CHECK(std::strcmp(name_ring_label(35), "9") == 0);
  // Ring order: DELETE (<), CONFIRM (C), CANCEL (X) after the 36 letters and digits.
  static_assert(kNameRingFirstControl == 36 && kNameRingDelete == 36 && kNameRingConfirm == 37 &&
                 kNameRingCancel == 38, "ring order: DELETE, CONFIRM, CANCEL");
  CHECK(std::strcmp(name_ring_label(kNameRingDelete), "<") == 0);
  CHECK(std::strcmp(name_ring_label(kNameRingConfirm), "C") == 0);
  CHECK(std::strcmp(name_ring_label(kNameRingCancel), "X") == 0);
  CHECK(std::strcmp(name_ring_label(kNameRingSize), "") == 0);
  CHECK(name_ring_char(0) == 'A' && name_ring_char(35) == '9');
  CHECK(name_ring_char(kNameRingConfirm) == 0 && name_ring_char(kNameRingDelete) == 0);
  const NameEditMessage messages[] = {NameEditMessage::Invalid, NameEditMessage::Taken,
                                      NameEditMessage::NoFreeSlot, NameEditMessage::NamesUnavailable,
                                      NameEditMessage::Failed, NameEditMessage::Busy};
  for (const NameEditMessage m : messages) CHECK(std::strlen(name_edit_message_text(m)) <= kFooterTextChars);
  CHECK(std::strcmp(name_edit_message_text(NameEditMessage::None), "") == 0);
}

int main() {
  move_parm();
  paging();
  formatting();
  colors_and_tables();
  scene_list_and_editor_text();
  if (g_failures == 0) std::printf("t11b_ui_model: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
