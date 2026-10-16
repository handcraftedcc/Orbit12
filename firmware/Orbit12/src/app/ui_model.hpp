#pragma once

// T11b UI model: bounded, copyable value data that core 0 assembles and core 1
// formats/renders (T12). No live module pointers, no Arduino includes. Text
// pointers refer to immutable static strings (labels, option tables).
//
// Source oracle: circuitpython-source/src/apps/Orion/core/{ui.py,state.py,
// neo_pixels.py,parms.py}. Rendering (layout, bitmaps, fonts) is T12; this file
// only carries the values those screens are built from, plus the pure helpers
// whose behaviour is source-defined (four-row paging, value formatting, key
// color tables).

#include <cstddef>
#include <cstdint>

#include "domain/music.hpp"
#include "domain/types.hpp"

namespace orbit {

constexpr std::uint8_t kParmsPerPage = 4;  // constants.PARMSPERPAGE
constexpr std::uint8_t kSlotCount = 10;    // constants.TOTALSLOTCOUNT

// state.ChainElements
enum Slot : std::uint8_t {
  kSlotSet = 0, kSlotIn = 1, kSlotP1 = 2, kSlotP2 = 3, kSlotP3 = 4, kSlotP4 = 5,
  kSlotP5 = 6, kSlotP6 = 7, kSlotOut = 8, kSlotSettings = 9
};
constexpr bool is_static_slot(std::uint8_t slot) noexcept {
  return slot == kSlotSet || slot == kSlotOut || slot == kSlotSettings;
}

enum class UiSection : std::uint8_t { Chain = 0, ParmSelection = 1, ParmEdit = 2, ModuleSelection = 3 };
enum class UiChainMode : std::uint8_t { Select = 0, Swap = 1 };

// How the source Parm type edits and displays a value.
enum class RowKind : std::uint8_t {
  Integer,   // IntParmType: clamp to minmax when bounded
  Decimal,   // FloatParmType: clamp, round(.,2)
  Percent,   // PercentParmType: value*100 + "%"
  Boolean,   // BooleanParmType: (v+d)%2, OFF/ON
  Enum,      // EnumParmType: (v+d)%count, option label
  Rate,      // RateParmType (rates only): (v+d)%13, rate label
  Pattern,   // PatternParmType: (v+d)%25, "PTN"+(v+1)
  Note,      // NoteParmType: clamp 0..11, note name
  Button     // ButtonParmType: ">" ; enter returns to selection
};

struct UiRow {
  const char* label = "";
  RowKind kind = RowKind::Integer;
  std::int32_t ivalue = 0;
  float fvalue = 0.0f;
  const char* const* options = nullptr;
  std::uint8_t option_count = 0;
  bool float_text = false;  // value is a Python float (prints "100.0"), not an int
  bool armed = false;   // RSTSCN/RESET "SUR?" display
  bool present = false; // false: blank row (index beyond parm count)
};

struct UiSlotInfo {
  const char* label = "EMPTY";  // Module.label (header/selector/cursor length)
  bool empty = true;            // module.name == "empty"
};

struct UiModel {
  UiSection section = UiSection::Chain;
  UiChainMode chain_mode = UiChainMode::Select;
  std::uint8_t chain_swap_mode = 0;    // 0 off, 1 pending, 2 active
  std::uint8_t active_chain = kSlotIn;
  std::int8_t active_parm = 0;         // -2 chain, -1 module row, 0.. parameters
  std::uint8_t parm_count = 0;
  std::uint8_t scroll_offset = 0;      // first visible parameter (four-row paging)
  // Controller sets raw active_parm - scroll_offset (may be negative or >= 4 for
  // chain/module rows and off-page cursors). The renderer does not use it: the
  // enlarged row and cursor come from active_parm, parm_count and scroll_offset.
  // Informational only; candidate for removal.
  std::int8_t highlight_row = 0;
  std::uint8_t row_count = 0;
  UiRow rows[kParmsPerPage]{};
  UiSlotInfo slots[kSlotCount]{};
  std::uint8_t selector_index = 0;     // module selector (source registry order)
  const char* selector_label = "EMPTY";
  std::uint8_t nav_keys_state = 0;     // 0 notes, 1 parameters
  bool transport_running = false;
  TransportMode transport_mode = TransportMode::Internal;
  std::uint8_t key = 0;
  std::uint8_t scale = 1;
  std::int32_t octave = 3;
  const char* key_custom_text = nullptr;  // drum input header text
  std::uint8_t active_scene = 0;
  float key_brightness = 1.0f;         // brightness_multiplier (KEYBRI)
  RgbColor pixels[kKeys]{};            // final NeoPixel values (brightness applied)
  bool encoder_down = false;
  std::uint32_t activity = 0;          // advances on every accepted input (wake)
  bool recovery = false;               // output/bridge recovery in progress
  bool busy = false;                   // scene operation running
};

static_assert(sizeof(UiModel) < 640, "UiModel is copied through the 3-slot snapshot queue");

// ---- State.move_active_parm_elem (pure) ----
std::int8_t move_parm_index(std::int8_t active_parm, int delta,
                            std::uint8_t parm_count, bool static_slot) noexcept;

// ---- Four-row paging: ParameterSection.get_active_parm_and_scroll /
// update_parm_selection / rebuild_parm_section. History dependent, so it lives
// on core 0 where every move is observed. ----
struct ParmPaging {
  std::uint8_t parm_count = 0;      // ParameterSection.parm_count (as last rebuilt)
  std::uint8_t scroll_offset = 0;
  std::int16_t last_active_parm = 0;
};
struct PagingResult { std::int16_t active; std::uint8_t scroll; };
PagingResult paging_active_and_scroll(const ParmPaging& paging, std::int16_t active_parm,
                                      std::uint8_t parm_count) noexcept;
void paging_rebuild(ParmPaging& paging, std::int16_t active_parm, std::uint8_t parm_count) noexcept;
void paging_update_selection(ParmPaging& paging, std::int16_t active_parm,
                             std::uint8_t parm_count) noexcept;

// ---- Module selector tables in source registry order (phraser hidden). ----
struct SelectorEntry { const char* id; const char* label; };
std::uint8_t processor_selector_count() noexcept;   // 18
const SelectorEntry& processor_selector(std::uint8_t index) noexcept;
std::uint8_t input_selector_count() noexcept;       // 6
const SelectorEntry& input_selector(std::uint8_t index) noexcept;
const char* module_label_for(std::uint8_t slot, const char* id) noexcept;  // Module.label

// ---- Option tables shared by system slots and inherited controls. ----
extern const char* const kOperationOptions[5];      // NEXT ADD OUT+N OUT+S SKIP
extern const char* const kInputOperationOptions[3]; // NEXT OUT+N ONLY
extern const char* const kSourceOptions[7];         // PREV IN S1..S5
extern const char* const kTransportModeOptions[2];  // INT EXT
extern const char* const kSceneOptions[10];         // 01..10
extern const char* const kCopySceneOptions[11];     // > 01..10
extern const char* const kResetOptions[2];          // > YES
extern const char* const kOnOffOptions[2];

// ---- Value formatting (Parm.get_display_value). Returns characters written
// (excluding NUL). Truncation to PARMVALUECHARS is the renderer's job. ----
std::size_t ui_format_value(const UiRow& row, char* out, std::size_t capacity) noexcept;

// ---- Key colors (neo_pixels.py) ----
RgbColor colorwheel(std::uint8_t position) noexcept;
RgbColor combine_color_and_brightness(RgbColor color, double brightness) noexcept;
extern const RgbColor kKeyColorBase, kKeyColorRoot, kNavNoteColors[12], kNavParmColors[12];
constexpr double kKeyBrightnessDefault = 0.3;
constexpr double kKeyBrightnessHeld = 1.0;

}  // namespace orbit
