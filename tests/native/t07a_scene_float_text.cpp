// T07a scene float text. The codec no longer calls printf, strtof or strtod.
// This test keeps those former calls as the reference and checks the allocation-free
// formatter and parser against them: output bytes, parsed bits and accept/reject
// decisions. Default run covers boundaries, source-derived rows, decades, random
// text and random midpoints. Run with --exhaustive to sweep all 2^31 finite
// non-negative binary32 patterns (threaded; several minutes).
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "persistence/scene_codec.hpp"

namespace {

int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

// ---- Former implementation, kept only here as the reference ----

// Former write path: snprintf("%.9g") of the widened binary32.
std::string ref_format(float value) {
  char text[32] = {};
  std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
  return text;
}

// Former parse_float on the pinned toolchain: strtof is (float)_strtod_l, so the
// reference widens through strtod. Whole text must be consumed and finite.
bool ref_parse_float(const char* text, std::size_t length, float& value) {
  if (length == 0 || length > 15) return false;
  char buffer[16] = {};
  std::memcpy(buffer, text, length);
  char* end = nullptr;
  const float result = static_cast<float>(std::strtod(buffer, &end));
  if (end != buffer + length || !std::isfinite(result)) return false;
  value = result;
  return true;
}

// Former parse_integer_like grammar (no leading space) then trunc(strtod()).
bool ref_parse_integer_like(const char* text, std::size_t length, std::int32_t& value) {
  if (length == 0 || length > 15) return false;
  std::size_t i = 0;
  if (text[i] == '+' || text[i] == '-') ++i;
  bool digits = false;
  while (i < length && text[i] >= '0' && text[i] <= '9') { ++i; digits = true; }
  if (i < length && text[i] == '.') {
    ++i;
    while (i < length && text[i] >= '0' && text[i] <= '9') { ++i; digits = true; }
  }
  if (!digits) return false;
  if (i < length && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    if (i < length && (text[i] == '+' || text[i] == '-')) ++i;
    const std::size_t exponent = i;
    while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
    if (i == exponent) return false;
  }
  if (i != length) return false;
  char buffer[16] = {};
  std::memcpy(buffer, text, length);
  char* end = nullptr;
  const double parsed = std::trunc(std::strtod(buffer, &end));
  if (end != buffer + length || !std::isfinite(parsed) || parsed < -2147483648.0 ||
      parsed > 2147483647.0) return false;
  value = static_cast<std::int32_t>(parsed);
  return true;
}

// ---- Helpers ----

std::uint32_t bits_of(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float float_of(std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string new_format(float value, bool& ok) {
  char text[32] = {};
  std::size_t length = 0;
  ok = orbit::scene_format_float(value, text, sizeof(text), length);
  return ok ? std::string(text, length) : std::string();
}

// Checks one value: formatted bytes equal the reference and the reference text parses back.
bool check_value(float value, std::uint64_t& mismatches) {
  if (!std::isfinite(value)) return true;  // outside the codec domain: writers refuse non-finite
  bool ok = false;
  const std::string produced = new_format(value, ok);
  const std::string expected = ref_format(value);
  if (!ok || produced != expected) {
    if (mismatches < 10) {
      std::fprintf(stderr, "format mismatch bits=%08" PRIx32 " new=%s ref=%s\n", bits_of(value),
                   produced.c_str(), expected.c_str());
    }
    ++mismatches;
    return false;
  }
  float parsed = 0.0f;
  if (!orbit::scene_parse_float(expected.data(), expected.size(), parsed) ||
      bits_of(parsed) != bits_of(value)) {
    if (mismatches < 10) {
      std::fprintf(stderr, "round trip mismatch bits=%08" PRIx32 " text=%s\n", bits_of(value),
                   expected.c_str());
    }
    ++mismatches;
    return false;
  }
  return true;
}

void expect_format(float value, const char* text) {
  bool ok = false;
  const std::string produced = new_format(value, ok);
  if (!ok || produced != text) {
    std::fprintf(stderr, "expected format %s got %s\n", text, produced.c_str());
    ++failures;
  }
}

void expect_parse(const char* text, bool accepted, std::uint32_t bits = 0) {
  float value = 0.0f;
  const std::size_t length = std::strlen(text);
  const bool got = orbit::scene_parse_float(text, length, value);
  if (got != accepted || (accepted && bits_of(value) != bits)) {
    std::fprintf(stderr, "parse %s: accepted=%d expected=%d bits=%08" PRIx32 " want=%08" PRIx32 "\n",
                 text, static_cast<int>(got), static_cast<int>(accepted), bits_of(value), bits);
    ++failures;
  }
}

void expect_integer(const char* text, bool accepted, std::int32_t expected = 0) {
  std::int32_t value = 0;
  const std::size_t length = std::strlen(text);
  const bool got = orbit::scene_parse_integer_like(text, length, value);
  if (got != accepted || (accepted && value != expected)) {
    std::fprintf(stderr, "integer %s: accepted=%d value=%" PRId32 "\n", text, static_cast<int>(got),
                 value);
    ++failures;
  }
  std::int32_t reference = 0;
  const bool ref_got = ref_parse_integer_like(text, length, reference);
  if (ref_got != got || (got && reference != value)) {
    std::fprintf(stderr, "integer %s differs from reference strtod path\n", text);
    ++failures;
  }
}

// ---- Groups ----

void fixed_text_checks() {
  expect_format(0.0f, "0");
  expect_format(-0.0f, "-0");
  expect_format(1.0f, "1");
  expect_format(0.5f, "0.5");
  expect_format(0.3f, "0.300000012");
  expect_format(1e-5f, "9.99999975e-06");  // binary32 1e-5 widened to binary64, nine digits
  expect_format(1e-4f, "9.99999975e-05");
  expect_format(1e15f, "9.99999987e+14");
  expect_format(100000.0625f, "100000.062");  // exact tie, round half to even
  expect_format(999999999.5f, "1e+09");        // carry changes the exponent
  expect_format(FLT_MAX, "3.40282347e+38");
  expect_format(-FLT_MAX, "-3.40282347e+38");
  expect_format(FLT_MIN, "1.17549435e-38");
  expect_format(FLT_TRUE_MIN, "1.40129846e-45");
  expect_format(16777216.0f, "16777216");
  expect_format(0.125f, "0.125");
  expect_format(0.05f, "0.0500000007");
  expect_format(100000.0f, "100000");
  expect_format(1000000000.0f, "1e+09");

  expect_parse("3.402823567e38", true, bits_of(FLT_MAX));
  expect_parse("3.402823568e38", false);     // beyond the half-ulp midpoint: overflow
  expect_parse("7e-46", true, 0);             // below 2^-150: rounds to zero
  expect_parse("7.006492322e-46", true, 1);   // above 2^-150: smallest subnormal
  expect_parse("1e-45", true, 1);
  expect_parse("16777217", true, bits_of(16777216.0f));  // tie to even
  expect_parse("0x1p-1", false);               // hex rejected (Python float rejects it too)
  expect_parse("0X1P-1", false);
  expect_parse("inf", false);
  expect_parse("nan", false);
  expect_parse("1e", false);
  expect_parse("1e+", false);
  expect_parse("1.5.2", false);
  expect_parse(" 0.5", true, bits_of(0.5f));  // strtof-compatible leading space
  expect_parse("0.5 ", false);
  expect_parse("1.", true, bits_of(1.0f));
  expect_parse(".5", true, bits_of(0.5f));
  expect_parse("-0", true, bits_of(-0.0f));
  expect_parse("-0.0", true, bits_of(-0.0f));
  expect_parse("1e-50", true, 0);             // underflow accepted as zero
  expect_parse("0e9999999999999", true, 0);   // 15 characters
  expect_parse("1e39", false);                // overflow
  expect_parse("0.000000000000001", false);   // 17 characters: over the length gate
  expect_parse("-", false);
  expect_parse("", false);
  expect_parse("+.e5", false);

  expect_integer("1.5e3", true, 1500);
  expect_integer("-2.9", true, -2);
  expect_integer("2147483647.9", true, 2147483647);
  expect_integer("-2147483648.9", true, INT32_MIN);
  expect_integer("2147483648", false);
  expect_integer("-2147483649", false);
  expect_integer("+7", true, 7);
  expect_integer("0.99", true, 0);
  expect_integer("-0.5", true, 0);
  expect_integer(" 3", false);
  expect_integer("1e10", false);
  expect_integer("1e", false);
}

// Source-derived float rows: every default, range bound and step used by the
// decimal parameter rows, plus their multiples, and decade sweeps.
void source_row_checks(std::uint64_t& mismatches) {
  struct Row { float minimum; float maximum; float step; };
  const Row rows[] = {
      {0.0f, 100000.0f, 5.0f},     // gate (arp, arp_walk, euclid, wander range)
      {0.0f, 100000.0f, 0.05f},
      {0.0f, 1.0f, 0.01f},         // swing and scene default step
      {0.0f, 1.0f, 0.05f},         // chance rows, delay mods
      {-1.0f, 1.0f, 0.05f},        // tilt
      {-1.0f, 1.0f, 0.01f},
      {0.0f, 0.5f, 0.5f},
      {-FLT_MAX, FLT_MAX, FLT_MAX / 16.0f},  // delay_bounce and chop extremes
  };
  const float literals[] = {0.0f, -0.0f, 0.05f, 0.5f, 1.0f, -1.0f, 5.0f, 100.0f, 100000.0f,
                            0.125f, 0.375f, 0.3f, 1e-5f, 1e15f, FLT_MAX, -FLT_MAX, FLT_MIN,
                            FLT_TRUE_MIN, 123456.7f, 16777217.0f, 2.5e-7f, 3.0e7f};
  for (const float value : literals) check_value(value, mismatches);
  for (const Row& row : rows) {
    for (int k = 0; k <= 40000; ++k) {
      const double value = static_cast<double>(row.minimum) + static_cast<double>(k) * row.step;
      if (value > static_cast<double>(row.maximum)) break;
      check_value(static_cast<float>(value), mismatches);
    }
    for (int k = 1; k <= 2000; ++k) {  // stepped multiples near the top of the range
      const double value = static_cast<double>(row.maximum) - static_cast<double>(k) * row.step;
      if (value < static_cast<double>(row.minimum)) break;
      check_value(static_cast<float>(value), mismatches);
    }
  }
  for (int exponent = -46; exponent <= 39; ++exponent) {
    for (int mantissa = 1; mantissa <= 9; ++mantissa) {
      for (const int extra : {0, 1, 5}) {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "%d.%de%d", mantissa, extra, exponent);
        float value = 0.0f;
        if (ref_parse_float(text, std::strlen(text), value)) check_value(value, mismatches);
        const float nearest = static_cast<float>(std::strtod(text, nullptr));
        check_value(nearest, mismatches);
      }
    }
  }
}

// Random finite binary32 patterns, plus negatives, checked with the reference.
void random_float_checks(std::uint64_t& mismatches) {
  std::mt19937_64 rng(0x5CE0C0DE1ULL);
  for (int i = 0; i < 300000; ++i) {
    const auto bits = static_cast<std::uint32_t>(rng() & 0x7FFFFFFFU);
    if (((bits >> 23) & 0xFFU) == 0xFFU) continue;
    check_value(float_of(bits), mismatches);
    check_value(float_of(bits | 0x80000000U), mismatches);
  }
}

// Midpoints between adjacent binary32 values, written with 8 to 14 significant digits.
// Those strings are exactly the double-rounding cases where (float)strtod differs
// from a single correct rounding. The reference is (float)strtod.
void midpoint_checks(std::uint64_t& mismatches, std::uint64_t& host_differs) {
  std::mt19937_64 rng(0xD0B1E0A7ULL);
  std::uint64_t accepted_strings = 0;
  for (int i = 0; i < 400000; ++i) {
    // Magnitudes from about 1e-4 to 1e7, where fixed notation fits in 15 characters.
    const auto exponent_field = static_cast<std::uint32_t>(113 + rng() % 36U);
    const auto fraction = static_cast<std::uint32_t>(rng() & 0x7FFFFFU);
    const std::uint32_t bits = (exponent_field << 23) | fraction;
    const float low = float_of(bits);
    const float high = float_of(bits + 1U);
    const double midpoint = (static_cast<double>(low) + static_cast<double>(high)) / 2.0;
    // Written with 12 to 15 significant digits: the nearest decimal lies within a
    // double ulp of the midpoint often enough for double rounding to matter.
    const int digits = 12 + static_cast<int>(rng() % 4U);
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.*g", digits, midpoint);
    const std::size_t length = std::strlen(text);
    if (length > 15 || std::strchr(text, 'e') != nullptr) continue;
    ++accepted_strings;
    float reference = 0.0f;
    if (!ref_parse_float(text, length, reference)) continue;
    float produced = 0.0f;
    if (!orbit::scene_parse_float(text, length, produced) || bits_of(produced) != bits_of(reference)) {
      if (mismatches < 10) {
        std::fprintf(stderr, "midpoint mismatch text=%s new=%08" PRIx32 " ref=%08" PRIx32 "\n",
                     text, bits_of(produced), bits_of(reference));
      }
      ++mismatches;
    }
    // Informational: host strtof rounds once; (float)strtod rounds twice (pinned newlib).
    if (std::strtof(text, nullptr) != reference) ++host_differs;
  }
  std::printf("midpoint strings checked: %" PRIu64 "\n", accepted_strings);
}

// Random short grammar strings, including rejects, against the reference.
void random_text_checks(std::uint64_t& mismatches) {
  std::mt19937_64 rng(0x7E47A11ULL);
  static const char digits[] = "0123456789";
  static const char others[] = ".eE+- ";
  static const char rejects[] = "xXnNiI";
  std::uint64_t checked = 0;
  for (int i = 0; i < 400000; ++i) {
    char text[16] = {};
    const std::size_t length = 1 + static_cast<std::size_t>(rng() % 15U);
    bool reject_class = false;
    for (std::size_t k = 0; k < length; ++k) {
      const auto pick = rng() % 100U;
      if (pick < 70U) {
        text[k] = digits[rng() % 10U];
      } else if (pick < 96U) {
        text[k] = others[rng() % (sizeof(others) - 1)];
      } else {
        text[k] = rejects[rng() % (sizeof(rejects) - 1)];
        reject_class = true;
      }
    }
    float reference = 0.0f;
    float produced = 0.0f;
    const bool ref_ok = ref_parse_float(text, length, reference);
    const bool new_ok = orbit::scene_parse_float(text, length, produced);
    if (reject_class) {
      // Only ' ' is allowed to be leading; letters never form decimal text.
      if (new_ok) {
        std::fprintf(stderr, "accepted letters: %.*s\n", static_cast<int>(length), text);
        ++mismatches;
      }
    } else if (ref_ok != new_ok || (new_ok && bits_of(reference) != bits_of(produced))) {
      if (mismatches < 10) {
        std::fprintf(stderr, "text mismatch '%.*s' new=%d ref=%d\n", static_cast<int>(length), text,
                     static_cast<int>(new_ok), static_cast<int>(ref_ok));
      }
      ++mismatches;
    }
    std::int32_t integer_reference = 0;
    std::int32_t integer_produced = 0;
    const bool iref = ref_parse_integer_like(text, length, integer_reference);
    const bool inew = orbit::scene_parse_integer_like(text, length, integer_produced);
    if (!reject_class && (iref != inew || (inew && integer_reference != integer_produced))) {
      if (mismatches < 10) {
        std::fprintf(stderr, "integer mismatch '%.*s'\n", static_cast<int>(length), text);
      }
      ++mismatches;
    }
    ++checked;
  }
  std::printf("random text strings checked: %" PRIu64 "\n", checked);
}

// Exhaustive sweep of every finite non-negative binary32 pattern, threaded.
int exhaustive_sweep() {
  const unsigned threads_hint = std::thread::hardware_concurrency();
  const unsigned threads = threads_hint == 0 ? 4U : threads_hint;
  std::vector<std::uint64_t> failed(threads, 0);
  std::vector<std::thread> workers;
  const std::uint64_t total = 0x7F800000ULL;  // non-negative finite patterns
  for (unsigned t = 0; t < threads; ++t) {
    workers.emplace_back([&, t]() {
      const std::uint64_t first = total * t / threads;
      const std::uint64_t last = total * (t + 1) / threads;
      for (std::uint64_t bits = first; bits < last; ++bits) {
        const auto pattern = static_cast<std::uint32_t>(bits);
        const float value = float_of(pattern);
        bool ok = false;
        char text[32] = {};
        std::size_t length = 0;
        ok = orbit::scene_format_float(value, text, sizeof(text), length);
        char expected[32] = {};
        std::snprintf(expected, sizeof(expected), "%.9g", static_cast<double>(value));
        if (!ok || std::strcmp(text, expected) != 0) {
          ++failed[t];
          continue;
        }
        float parsed = 0.0f;
        if (!orbit::scene_parse_float(text, length, parsed) || bits_of(parsed) != pattern) {
          ++failed[t];
        }
        // Negative pattern: same digits with a leading minus, and the parser agrees.
        const float negative = float_of(pattern | 0x80000000U);
        char negative_text[33] = {};
        std::snprintf(negative_text, sizeof(negative_text), "-%s", text);
        float negative_parsed = 0.0f;
        if (!orbit::scene_format_float(negative, text, sizeof(text), length) ||
            std::strcmp(text, negative_text) != 0 ||
            !orbit::scene_parse_float(text, length, negative_parsed) ||
            bits_of(negative_parsed) != (pattern | 0x80000000U)) {
          ++failed[t];
        }
      }
    });
  }
  for (auto& worker : workers) worker.join();
  std::uint64_t mismatches = 0;
  for (const auto count : failed) mismatches += count;
  std::printf("exhaustive binary32 sweep: threads=%u patterns=%" PRIu64 " mismatches=%" PRIu64 "\n",
              threads, total, mismatches);
  return mismatches == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--exhaustive") == 0) return exhaustive_sweep();

  std::uint64_t mismatches = 0;
  std::uint64_t host_differs = 0;
  fixed_text_checks();
  source_row_checks(mismatches);
  random_float_checks(mismatches);
  midpoint_checks(mismatches, host_differs);
  random_text_checks(mismatches);
  std::printf("host strtof differs from (float)strtod on %" PRIu64 " midpoint strings (informational)\n",
              host_differs);
  CHECK(mismatches == 0);
  if (failures || mismatches) {
    std::fprintf(stderr, "t07a_scene_float_text: %d check(s) failed, %" PRIu64 " reference mismatch(es)\n",
                 failures, mismatches);
    return 1;
  }
  std::puts("T07a scene float text checks passed");
  return 0;
}
