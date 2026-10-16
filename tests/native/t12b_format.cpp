// T12b value formatting: ui_format_value (float-free) against the pre-T12b
// snprintf implementation, swept over each row type's value range. The
// snprintf reference lives only in this file; it is never linked into firmware.
// Python-semantics anchors (round-half-even, repr of round(v, 2)) are asserted
// separately. Rows store float32, so percent values are rounded from the
// float32 value (the Python source uses a double); see the 0.025 anchor.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "app/ui_model.hpp"

static int g_failures = 0;
static long g_mismatches = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

using namespace orbit;

namespace {

void copy_bounded(char* out, std::size_t cap, const char* text) {
  std::size_t n = std::strlen(text);
  if (n >= cap) n = cap - 1;
  std::memcpy(out, text, n);
  out[n] = '\0';
}

// ---- Reference: the previous snprintf-based formatter (test only). ----
void ref_integer(std::int32_t v, char* out, std::size_t cap) {
  char tmp[32];
  std::snprintf(tmp, sizeof tmp, "%ld", static_cast<long>(v));
  copy_bounded(out, cap, tmp);
}

void ref_pattern(std::int32_t v, char* out, std::size_t cap) {
  char tmp[32];
  std::snprintf(tmp, sizeof tmp, "PTN%ld", static_cast<long>(v) + 1);
  copy_bounded(out, cap, tmp);
}

void ref_decimal(float fv, bool float_text, char* out, std::size_t cap) {
  char tmp[48];
  if (!float_text && fv == std::floor(fv) && std::fabs(fv) < 1e15f) {
    std::snprintf(tmp, sizeof tmp, "%.0f", static_cast<double>(fv));
  } else {
    std::snprintf(tmp, sizeof tmp, "%.2f", static_cast<double>(fv));
    std::size_t len = std::strlen(tmp);
    while (len > 0 && tmp[len - 1] == '0' && len >= 2 && tmp[len - 2] != '.') --len;
    tmp[len] = '\0';
  }
  copy_bounded(out, cap, tmp);
}

void ref_percent(float fv, char* out, std::size_t cap) {
  char tmp[48];
  std::snprintf(tmp, sizeof tmp, "%.0f%%", std::nearbyint(static_cast<double>(fv) * 100.0));
  copy_bounded(out, cap, tmp);
}

UiRow row_of(RowKind kind) {
  UiRow r;
  r.kind = kind;
  r.present = true;
  return r;
}

// Compare new output with the reference. Negative zero results are the only
// intended divergence: the snprintf reference prints "-0" / "-0%", while the
// Python source prints "0" / "0%" (int 0, and round() of a negative zero).
void expect_same(const char* what, const UiRow& row, const char* reference, const char* got) {
  const char* expected = reference;
  char fixed[16];
  if (std::strcmp(reference, "-0") == 0 || std::strcmp(reference, "-0%") == 0) {
    copy_bounded(fixed, sizeof fixed, reference + 1);
    expected = fixed;
  }
  if (std::strcmp(expected, got) != 0) {
    if (g_mismatches < 10)
      std::fprintf(stderr, "%s mismatch: reference \"%s\" got \"%s\" (fvalue=%.9g ivalue=%d ft=%d)\n",
                   what, reference, got, static_cast<double>(row.fvalue), static_cast<int>(row.ivalue),
                   row.float_text ? 1 : 0);
    ++g_mismatches;
  }
}

void check_decimal(float fv, bool float_text) {
  UiRow row = row_of(RowKind::Decimal);
  row.fvalue = fv;
  row.float_text = float_text;
  char ref[48];
  char got[48];
  ref_decimal(fv, float_text, ref, sizeof ref);
  const std::size_t n = ui_format_value(row, got, sizeof got);
  CHECK(n == std::strlen(got));
  expect_same("decimal", row, ref, got);
}

void check_percent(float fv) {
  UiRow row = row_of(RowKind::Percent);
  row.fvalue = fv;
  char ref[48];
  char got[48];
  ref_percent(fv, ref, sizeof ref);
  ui_format_value(row, got, sizeof got);
  expect_same("percent", row, ref, got);
}

void text_of(const UiRow& row, const char* expected) {
  char got[48];
  ui_format_value(row, got, sizeof got);
  if (std::strcmp(got, expected) != 0)
    std::fprintf(stderr, "expected \"%s\" got \"%s\"\n", expected, got);
  CHECK(std::strcmp(got, expected) == 0);
}

// Python anchors: expected strings are what the source displays.
void python_semantics() {
  UiRow d = row_of(RowKind::Decimal);
  d.float_text = true;
  d.fvalue = 0.05f;   text_of(d, "0.05");
  d.fvalue = 100.0f;  text_of(d, "100.0");
  d.fvalue = 0.125f;  text_of(d, "0.12");   // round(0.125, 2): exact tie to even
  d.fvalue = 0.375f;  text_of(d, "0.38");   // exact tie to even (upwards)
  d.fvalue = 2.675f;  text_of(d, "2.67");   // binary value is just below the tie
  d.fvalue = -0.001f; text_of(d, "-0.0");   // round(-0.001, 2) == -0.0
  d.fvalue = -1.5f;   text_of(d, "-1.5");
  d.fvalue = 1.10f;   text_of(d, "1.1");
  d.fvalue = 0.0f;    text_of(d, "0.0");
  d.fvalue = -0.0f;   text_of(d, "-0.0");
  d.fvalue = 1.0e6f;  text_of(d, "1000000.0");
  UiRow i = row_of(RowKind::Decimal);
  i.fvalue = -0.0f;   text_of(i, "0");      // Python int 0
  i.fvalue = 127.0f;  text_of(i, "127");
  i.fvalue = -40.0f;  text_of(i, "-40");

  UiRow p = row_of(RowKind::Percent);
  p.fvalue = 0.5f;    text_of(p, "50%");
  // float32 0.025 is 0.02500000044..., so it rounds to 3. The Python double
  // 0.025 * 100 is exactly 2.5 and rounds to 2; the float32 storage limit is
  // a known difference for percent values sitting exactly on a tie.
  p.fvalue = 0.025f;  text_of(p, "3%");
  p.fvalue = 0.125f;  text_of(p, "12%");    // 12.5 rounds to even
  p.fvalue = 0.375f;  text_of(p, "38%");    // 37.5 rounds to even (upwards)
  p.fvalue = -0.004f; text_of(p, "0%");     // round() of a negative zero prints 0
  p.fvalue = 1.25f;   text_of(p, "125%");

  UiRow b = row_of(RowKind::Boolean);
  b.ivalue = 0;       text_of(b, "OFF");
  b.ivalue = 1;       text_of(b, "ON");

  UiRow pt = row_of(RowKind::Pattern);
  pt.ivalue = 0;      text_of(pt, "PTN1");
  pt.ivalue = 24;     text_of(pt, "PTN25");

  UiRow btn = row_of(RowKind::Button);
  text_of(btn, ">");

  UiRow nan = row_of(RowKind::Decimal);
  nan.fvalue = std::numeric_limits<float>::quiet_NaN();
  text_of(nan, "ERR");
  UiRow inf = row_of(RowKind::Percent);
  inf.fvalue = std::numeric_limits<float>::infinity();
  text_of(inf, "ERR");

  UiRow hidden = row_of(RowKind::Decimal);
  hidden.present = false;
  hidden.fvalue = 3.0f;
  text_of(hidden, "");

  // Bounded output: capacity keeps cap-1 characters and NUL-terminates.
  char small[4];
  d.fvalue = 100.0f;
  const std::size_t n = ui_format_value(d, small, sizeof small);
  CHECK(n == 3 && std::strcmp(small, "100") == 0);
  CHECK(ui_format_value(d, small, 0) == 0);
}

// Full-range sweeps against the reference.
void sweep_integer() {
  UiRow row = row_of(RowKind::Integer);
  char ref[32];
  char got[32];
  for (std::int64_t v = INT32_MIN; v <= INT32_MAX; v += 1009) {
    row.ivalue = static_cast<std::int32_t>(v);
    ref_integer(row.ivalue, ref, sizeof ref);
    ui_format_value(row, got, sizeof got);
    expect_same("integer", row, ref, got);
  }
  for (std::int32_t v : {INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX}) {
    row.ivalue = v;
    ref_integer(v, ref, sizeof ref);
    ui_format_value(row, got, sizeof got);
    expect_same("integer edge", row, ref, got);
  }
}

void sweep_pattern() {
  UiRow row = row_of(RowKind::Pattern);
  char ref[32];
  char got[32];
  // INT32_MAX + 1 overflows the reference's 32-bit long; Python has no such limit.
  for (std::int64_t v = INT32_MIN; v < INT32_MAX; v += 997) {
    row.ivalue = static_cast<std::int32_t>(v);
    ref_pattern(row.ivalue, ref, sizeof ref);
    ui_format_value(row, got, sizeof got);
    expect_same("pattern", row, ref, got);
  }
}

void sweep_decimal_grid() {
  // Every two-decimal value in +-10000.00 (Python int- and float-typed).
  for (int k = -1000000; k <= 1000000; ++k) {
    const float fv = static_cast<float>(static_cast<double>(k) / 100.0);
    check_decimal(fv, true);
    check_decimal(fv, false);
  }
  // Exact binary ties at the third decimal (n/8, n odd): round-half-even.
  for (int k = -80000; k <= 80000; ++k) {
    const float fv = static_cast<float>(static_cast<double>(2 * k + 1) / 8.0);
    check_decimal(fv, true);
    check_decimal(fv, false);
    check_percent(fv);
  }
  // Integral magnitudes up to 1e15, Python int- and float-typed.
  for (double v = 0.0; v < 1e15; v = v * 1.37 + 1.0) {
    check_decimal(static_cast<float>(v), true);
    check_decimal(static_cast<float>(v), false);
    check_decimal(static_cast<float>(-v), false);
  }
}

// Every float32 bit pattern in the 1e-6..1e6 magnitude band (stride 1009),
// both signs, through every float-typed row kind.
void sweep_decimal_bits() {
  std::uint32_t lo = 0;
  std::uint32_t hi = 0;
  const float low = 1e-6f;
  const float high = 1e6f;
  std::memcpy(&lo, &low, sizeof lo);
  std::memcpy(&hi, &high, sizeof hi);
  for (std::uint32_t bits = lo; bits <= hi; bits += 1009U) {
    float fv = 0.0f;
    std::memcpy(&fv, &bits, sizeof fv);
    check_decimal(fv, true);
    check_decimal(fv, false);
    check_percent(fv);
    check_decimal(-fv, true);
    check_decimal(-fv, false);
    check_percent(-fv);
  }
}

// Random float32 values below 1e15 (xorshift32, fixed seed).
void sweep_decimal_random() {
  std::uint32_t state = 0x2545F491U;
  long accepted = 0;
  while (accepted < 300000L) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    float fv = 0.0f;
    std::memcpy(&fv, &state, sizeof fv);
    if (!std::isfinite(fv) || std::fabs(fv) >= 1e15f) continue;
    ++accepted;
    check_decimal(fv, true);
    check_decimal(fv, false);
    check_percent(fv);
  }
}

void enum_and_rate_text() {
  static const char* const kOptions[3] = {"NEXT", "ADD", "OUT+N"};
  UiRow row = row_of(RowKind::Enum);
  row.options = kOptions;
  row.option_count = 3;
  for (std::int32_t v = 0; v < 3; ++v) {
    row.ivalue = v;
    text_of(row, kOptions[v]);
  }
  row.ivalue = 3;  // out of range: blank, never a read past the table
  text_of(row, "");
  row.armed = true;
  row.ivalue = 0;
  text_of(row, "SUR?");

  UiRow rate = row_of(RowKind::Rate);
  rate.ivalue = 0;
  text_of(rate, "1/1");
  rate.ivalue = 12;
  text_of(rate, "1/64");

  UiRow note = row_of(RowKind::Note);
  note.ivalue = 9;
  text_of(note, "A");
  note.ivalue = 11;
  text_of(note, "B");
}

}  // namespace

int main() {
  python_semantics();
  sweep_integer();
  sweep_pattern();
  sweep_decimal_grid();
  sweep_decimal_bits();
  sweep_decimal_random();
  enum_and_rate_text();
  if (g_failures != 0 || g_mismatches != 0) {
    std::fprintf(stderr, "t12b_format: %d failure(s), %ld reference mismatch(es)\n", g_failures, g_mismatches);
    return 1;
  }
  std::printf("t12b_format: Python anchors and full-range snprintf-reference sweeps passed\n");
  return 0;
}
