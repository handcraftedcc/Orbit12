#include "app/ui_model.hpp"

#include <cstdint>
#include <cstring>

namespace orbit {

const char* const kOperationOptions[5] = {"NEXT", "ADD", "OUT+N", "OUT+S", "SKIP"};
const char* const kInputOperationOptions[3] = {"NEXT", "OUT+N", "ONLY"};
const char* const kSourceOptions[7] = {"PREV", "IN", "S1", "S2", "S3", "S4", "S5"};
const char* const kTransportModeOptions[2] = {"INT", "EXT"};
const char* const kSceneOptions[10] = {"01", "02", "03", "04", "05", "06", "07", "08", "09", "10"};
const char* const kCopySceneOptions[11] = {">", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10"};
const char* const kResetOptions[2] = {">", "YES"};
const char* const kOnOffOptions[2] = {"OFF", "ON"};

namespace {
// registry._registry.MODULE_SPECS with phraser (no implementation) removed.
constexpr SelectorEntry kProcessorSelectors[] = {
    {"empty", "EMPTY"}, {"arp", "ARP"}, {"arp_walk", "ARPWLK"}, {"bounce", "BOUNCE"},
    {"chord", "CHRD"}, {"chopper", "CHOPPR"}, {"delay", "DELAY"}, {"drop", "DROP"},
    {"euclid", "EUCLID"}, {"gate", "GATE"}, {"latch", "LATCH"}, {"pick", "PICK"},
    {"quantize", "QNT"}, {"randomize", "RND"}, {"retrigger", "RTRGR"}, {"strum", "STRUM"},
    {"transpose", "TRNS"}, {"wander", "WANDR"}};
constexpr SelectorEntry kInputSelectors[] = {
    {"note", "NOTE"}, {"chords", "CHRD"}, {"drum", "DRUM"}, {"matrix", "MTRX"},
    {"note_walk", "NOTEWLK"}, {"chord_walk", "CHRDWLK"}};
bool same(const char* a, const char* b) noexcept {
  return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}
}  // namespace

std::uint8_t processor_selector_count() noexcept {
  return static_cast<std::uint8_t>(sizeof(kProcessorSelectors) / sizeof(kProcessorSelectors[0]));
}
const SelectorEntry& processor_selector(std::uint8_t index) noexcept {
  return kProcessorSelectors[index < processor_selector_count() ? index : 0];
}
std::uint8_t input_selector_count() noexcept {
  return static_cast<std::uint8_t>(sizeof(kInputSelectors) / sizeof(kInputSelectors[0]));
}
const SelectorEntry& input_selector(std::uint8_t index) noexcept {
  return kInputSelectors[index < input_selector_count() ? index : 0];
}
const char* module_label_for(std::uint8_t slot, const char* id) noexcept {
  if (slot == kSlotSet) return "SET";
  if (slot == kSlotOut) return "OUT";
  if (slot == kSlotSettings) return "STTNGS";
  if (slot == kSlotIn) {
    for (const auto& e : kInputSelectors) if (same(e.id, id)) return e.label;
    return "NOTE";
  }
  for (const auto& e : kProcessorSelectors) if (same(e.id, id)) return e.label;
  return "EMPTY";
}

std::int8_t move_parm_index(std::int8_t active_parm, int delta,
                            std::uint8_t parm_count, bool static_slot) noexcept {
  // ((active + 2 + delta) % (count + 2)) - 2 with Python's floor modulo.
  const std::int32_t modulus = static_cast<std::int32_t>(parm_count) + 2;
  std::int32_t value = (static_cast<std::int32_t>(active_parm) + 2 + delta % modulus) % modulus;
  if (value < 0) value += modulus;
  std::int32_t next = value - 2;
  if (next == -1 && static_slot) next = (active_parm < next) ? 0 : -2;
  return static_cast<std::int8_t>(next);
}

PagingResult paging_active_and_scroll(const ParmPaging& paging, std::int16_t active_parm,
                                      std::uint8_t parm_count) noexcept {
  std::int16_t active = active_parm;
  if (active < 0) active = 0;
  else if (active >= parm_count) active = static_cast<std::int16_t>(parm_count - 1);
  const bool forward = !(active < paging.last_active_parm);  // equal counts as forward
  const std::int16_t preferred_row = forward ? kParmsPerPage - 2 : 1;
  const std::int16_t max_scroll = parm_count > kParmsPerPage ? parm_count - kParmsPerPage : 0;
  std::int16_t scroll = static_cast<std::int16_t>(active - preferred_row);
  if (scroll < 0) scroll = 0;
  else if (scroll > max_scroll) scroll = max_scroll;
  return {active, static_cast<std::uint8_t>(scroll)};
}

void paging_rebuild(ParmPaging& paging, std::int16_t active_parm, std::uint8_t parm_count) noexcept {
  paging.parm_count = parm_count;
  if (parm_count == 0) return;
  auto result = paging_active_and_scroll(paging, active_parm, parm_count);
  const std::int16_t max_scroll = parm_count > kParmsPerPage ? parm_count - kParmsPerPage : 0;
  if (result.active == paging.last_active_parm && paging.scroll_offset <= max_scroll &&
      paging.scroll_offset <= result.active && result.active < paging.scroll_offset + kParmsPerPage)
    result.scroll = paging.scroll_offset;  // keep the page when the active row is still visible
  paging.scroll_offset = result.scroll;
  paging.last_active_parm = result.active;
}

void paging_update_selection(ParmPaging& paging, std::int16_t active_parm,
                             std::uint8_t parm_count) noexcept {
  if (parm_count == 0) { paging_rebuild(paging, active_parm, parm_count); return; }
  const auto result = paging_active_and_scroll(paging, active_parm, parm_count);
  if (parm_count != paging.parm_count || result.scroll != paging.scroll_offset) {
    paging_rebuild(paging, active_parm, parm_count);
    return;
  }
  paging.last_active_parm = result.active;
}

namespace {
// Float-free bounded text sink. The render path must not reach newlib's
// printf float/dtoa code (heap + flash), so value text is built from integers
// only. Keeps at most cap-1 characters and always NUL-terminates when cap > 0.
class Sink {
 public:
  Sink(char* out, std::size_t cap) noexcept : out_(out), cap_(cap) {
    if (cap_ != 0) out_[0] = '\0';
  }
  void push(char c) noexcept {
    if (cap_ != 0 && len_ + 1 < cap_) out_[len_++] = c;
  }
  void text(const char* s) noexcept {
    while (*s != '\0') push(*s++);
  }
  void unsigned_decimal(std::uint64_t v) noexcept {
    char tmp[20];
    std::size_t n = 0;
    do {
      tmp[n++] = static_cast<char>('0' + v % 10U);
      v /= 10U;
    } while (v != 0);
    while (n != 0) push(tmp[--n]);
  }
  void signed_decimal(std::int64_t v) noexcept {
    if (v < 0) {
      push('-');
      unsigned_decimal(std::uint64_t{0} - static_cast<std::uint64_t>(v));
    } else {
      unsigned_decimal(static_cast<std::uint64_t>(v));
    }
  }
  std::size_t finish() noexcept {
    if (cap_ != 0) out_[len_] = '\0';
    return len_;
  }

 private:
  char* out_;
  std::size_t cap_;
  std::size_t len_ = 0;
};

static_assert(sizeof(float) == 4, "IEEE-754 single precision is required");

// Exact decomposition of a finite float: |value| = mantissa * 2^exponent.
struct FloatParts {
  bool finite;
  bool negative;
  std::uint32_t mantissa;  // < 2^24
  int exponent;
};

FloatParts split_float(float f) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof bits);
  FloatParts p{};
  p.negative = (bits >> 31) != 0U;
  const std::uint32_t biased = (bits >> 23) & 0xFFU;
  const std::uint32_t fraction = bits & 0x7FFFFFU;
  p.finite = biased != 0xFFU;
  if (biased == 0U) {
    p.mantissa = fraction;
    p.exponent = -149;
  } else {
    p.mantissa = fraction | 0x800000U;
    p.exponent = static_cast<int>(biased) - 150;
  }
  return p;
}

// True when |value| has no fractional part.
bool is_integral(const FloatParts& p) noexcept {
  if (p.mantissa == 0U || p.exponent >= 0) return true;
  const int shift = -p.exponent;
  if (shift >= 64) return false;
  return (static_cast<std::uint64_t>(p.mantissa) & ((std::uint64_t{1} << shift) - 1U)) == 0U;
}

// Integer part of |value|; caller guarantees integral and exponent <= 40.
std::uint64_t integer_magnitude(const FloatParts& p) noexcept {
  if (p.mantissa == 0U) return 0;
  if (p.exponent >= 0) return static_cast<std::uint64_t>(p.mantissa) << p.exponent;
  return static_cast<std::uint64_t>(p.mantissa) >> (-p.exponent);
}

// round-half-even(|value| * 100) on the exact binary value, i.e. the integer
// Python's round(x * 100) yields for the source float. Also the two-decimal
// digits for round(x, 2). False when the result would not fit in 64 bits.
bool round_times_100(const FloatParts& p, std::uint64_t& out) noexcept {
  out = 0;
  if (!p.finite) return false;
  if (p.mantissa == 0U) return true;
  if (p.exponent >= 0) {
    if (p.exponent > 33) return false;  // mantissa * 2^33 * 100 still fits in 64 bits
    out = (static_cast<std::uint64_t>(p.mantissa) << p.exponent) * 100U;
    return true;
  }
  const int shift = -p.exponent;
  if (shift >= 64) return true;  // |value| * 100 < 2^-33 rounds to zero
  const std::uint64_t scaled = static_cast<std::uint64_t>(p.mantissa) * 100U;  // < 2^31
  std::uint64_t q = scaled >> shift;
  const std::uint64_t rem = scaled & ((std::uint64_t{1} << shift) - 1U);
  const std::uint64_t half = std::uint64_t{1} << (shift - 1);
  if (rem > half || (rem == half && (q & 1U) != 0U)) ++q;
  out = q;
  return true;
}

// IntParm/FloatParm display (Python str() of the stored value). Python-int
// values print as integers ("0", "-12"); Python-float values print as
// repr(round(v, 2)): at least one fractional digit ("100.0", "0.05", "-0.1").
void format_decimal(const UiRow& row, Sink& sink) noexcept {
  const FloatParts p = split_float(row.fvalue);
  if (!p.finite) {
    sink.text("ERR");
    return;
  }
  if (!row.float_text && is_integral(p)) {
    if (p.mantissa != 0U && p.exponent > 40) {
      sink.text("ERR");
      return;
    }
    const std::uint64_t magnitude = integer_magnitude(p);
    if (p.negative && magnitude != 0U) sink.push('-');
    sink.unsigned_decimal(magnitude);
    return;
  }
  std::uint64_t hundredths = 0;
  if (!round_times_100(p, hundredths)) {
    sink.text("ERR");
    return;
  }
  if (p.negative) sink.push('-');
  sink.unsigned_decimal(hundredths / 100U);
  const std::uint64_t frac = hundredths % 100U;
  sink.push('.');
  if (frac == 0U) {
    sink.push('0');
  } else {
    sink.push(static_cast<char>('0' + frac / 10U));
    if (frac % 10U != 0U) sink.push(static_cast<char>('0' + frac % 10U));
  }
}

// PercentParmType: str(round(value * 100)) + "%". round() of a negative zero
// result is the integer 0, so no sign is printed for it.
void format_percent(const UiRow& row, Sink& sink) noexcept {
  const FloatParts p = split_float(row.fvalue);
  std::uint64_t rounded = 0;
  if (!round_times_100(p, rounded)) {
    sink.text("ERR");
    return;
  }
  if (p.negative && rounded != 0U) sink.push('-');
  sink.unsigned_decimal(rounded);
  sink.push('%');
}
}  // namespace

std::size_t ui_format_value(const UiRow& row, char* out, std::size_t capacity) noexcept {
  Sink sink(out, capacity);
  if (!row.present) return sink.finish();
  switch (row.kind) {
    case RowKind::Button:
      sink.text(">");
      break;
    case RowKind::Boolean:
      sink.text(row.ivalue == 0 ? "OFF" : "ON");
      break;
    case RowKind::Percent:
      format_percent(row, sink);
      break;
    case RowKind::Decimal:
      format_decimal(row, sink);
      break;
    case RowKind::Enum:
      if (row.armed) {
        sink.text("SUR?");
      } else if (row.options != nullptr && row.ivalue >= 0 && row.ivalue < row.option_count) {
        sink.text(row.options[row.ivalue]);
      }
      break;
    case RowKind::Rate:
      if (row.ivalue >= 0 && row.ivalue < static_cast<std::int32_t>(music::kRateLabels.size()))
        sink.text(music::kRateLabels[static_cast<std::size_t>(row.ivalue)]);
      break;
    case RowKind::Pattern:
      sink.text("PTN");
      sink.signed_decimal(static_cast<std::int64_t>(row.ivalue) + 1);
      break;
    case RowKind::Note:
      if (row.ivalue >= 0 && row.ivalue < 12)
        sink.text(music::kNotes[static_cast<std::size_t>(row.ivalue)]);
      break;
    case RowKind::Integer:
      sink.signed_decimal(row.ivalue);
      break;
  }
  return sink.finish();
}

RgbColor colorwheel(std::uint8_t pos) noexcept {
  // rainbowio.colorwheel
  if (pos < 85) return {static_cast<std::uint8_t>(255 - pos * 3), static_cast<std::uint8_t>(pos * 3), 0};
  if (pos < 170) {
    pos = static_cast<std::uint8_t>(pos - 85);
    return {0, static_cast<std::uint8_t>(255 - pos * 3), static_cast<std::uint8_t>(pos * 3)};
  }
  pos = static_cast<std::uint8_t>(pos - 170);
  return {static_cast<std::uint8_t>(pos * 3), 0, static_cast<std::uint8_t>(255 - pos * 3)};
}

RgbColor combine_color_and_brightness(RgbColor c, double b) noexcept {
  auto scale = [b](std::uint8_t v) {
    const double x = static_cast<double>(v) * b;
    return static_cast<std::uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : static_cast<int>(x)));  // int() truncation
  };
  return {scale(c.red), scale(c.green), scale(c.blue)};
}

namespace {
RgbColor stop_c() { return colorwheel(0); }
RgbColor start_c() { return colorwheel(85); }
RgbColor jump_in() { return colorwheel(32); }
RgbColor jump_out() { return colorwheel(52); }
RgbColor swap_c() { return colorwheel(191); }
RgbColor nav_note() { return colorwheel(150); }
RgbColor nav_parm() { return colorwheel(21); }
RgbColor enter_c() { return colorwheel(42); }
constexpr RgbColor kWhite{255, 255, 255};
constexpr RgbColor kOff{0, 0, 0};
}  // namespace

const RgbColor kKeyColorBase = colorwheel(128);
const RgbColor kKeyColorRoot = colorwheel(64);
// NAV_NOTE_COLORS / NAV_PARM_COLORS (index = physical key 0..11).
const RgbColor kNavNoteColors[12] = {
    stop_c(), start_c(), jump_in(), swap_c(), kWhite, jump_out(),
    nav_parm(), nav_note(), kOff, nav_note(), nav_note(), nav_note()};
const RgbColor kNavParmColors[12] = {
    stop_c(), start_c(), jump_in(), swap_c(), kWhite, jump_out(),
    nav_note(), nav_parm(), enter_c(), nav_parm(), nav_parm(), nav_parm()};

}  // namespace orbit
