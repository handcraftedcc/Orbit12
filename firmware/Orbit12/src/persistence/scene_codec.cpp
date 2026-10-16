#include "scene_codec.hpp"

#include <cmath>
#include <cstring>

namespace orbit {
namespace {

// ---- Allocation-free exact decimal <-> binary32 text conversion ----
// Replaces snprintf("%.9g"), strtof and strtod on the target. Those pull newlib
// printf/dtoa/Balloc/malloc into the image and need a large stack. Bignums here
// are fixed little-endian arrays of 32-bit limbs. The largest value used is
// below 2^260 (see the bounds at each call site), so 12 limbs is a hard bound.

constexpr std::size_t kBigLimbs = 12;
constexpr int kBigBits = static_cast<int>(kBigLimbs * 32);

struct Big {
  std::uint32_t limb[kBigLimbs]{};
};

void big_set(Big& value, std::uint64_t number) noexcept {
  value = Big{};
  value.limb[0] = static_cast<std::uint32_t>(number);
  value.limb[1] = static_cast<std::uint32_t>(number >> 32);
}

bool big_is_zero(const Big& value) noexcept {
  for (const std::uint32_t limb : value.limb) {
    if (limb != 0) return false;
  }
  return true;
}

int big_bit_length(const Big& value) noexcept {
  for (std::size_t i = kBigLimbs; i-- > 0;) {
    if (value.limb[i] == 0) continue;
    int bits = 0;
    for (std::uint32_t top = value.limb[i]; top != 0; top >>= 1) ++bits;
    return static_cast<int>(i) * 32 + bits;
  }
  return 0;
}

bool big_bit(const Big& value, int index) noexcept {
  if (index < 0 || index >= kBigBits) return false;
  return ((value.limb[index / 32] >> (index % 32)) & 1U) != 0;
}

// True when any bit with index in [0, bound) is set.
bool big_any_below(const Big& value, int bound) noexcept {
  if (bound <= 0) return false;
  if (bound > kBigBits) bound = kBigBits;
  const auto whole = static_cast<std::size_t>(bound / 32);
  for (std::size_t i = 0; i < whole; ++i) {
    if (value.limb[i] != 0) return true;
  }
  const int partial = bound % 32;
  return partial != 0 && (value.limb[whole] & ((1U << partial) - 1U)) != 0;
}

void big_mul_add(Big& value, std::uint32_t factor, std::uint32_t addend) noexcept {
  std::uint64_t carry = addend;
  for (auto& limb : value.limb) {
    const std::uint64_t product = static_cast<std::uint64_t>(limb) * factor + carry;
    limb = static_cast<std::uint32_t>(product);
    carry = product >> 32;
  }
}

std::uint32_t big_div_small(Big& value, std::uint32_t divisor) noexcept {
  std::uint64_t remainder = 0;
  for (std::size_t i = kBigLimbs; i-- > 0;) {
    const std::uint64_t current = (remainder << 32) | value.limb[i];
    value.limb[i] = static_cast<std::uint32_t>(current / divisor);
    remainder = current % divisor;
  }
  return static_cast<std::uint32_t>(remainder);
}

void big_shift_left(Big& value, int shift) noexcept {
  Big out{};
  const auto limbs = static_cast<std::size_t>(shift / 32);
  const int bits = shift % 32;
  for (std::size_t src = 0; src < kBigLimbs; ++src) {
    const std::size_t dst = src + limbs;
    if (dst >= kBigLimbs) break;
    const std::uint64_t part = static_cast<std::uint64_t>(value.limb[src]) << bits;
    out.limb[dst] |= static_cast<std::uint32_t>(part);
    if (dst + 1 < kBigLimbs) out.limb[dst + 1] |= static_cast<std::uint32_t>(part >> 32);
  }
  value = out;
}

void big_shift_right(Big& value, int shift) noexcept {
  Big out{};
  const auto limbs = static_cast<std::size_t>(shift / 32);
  const int bits = shift % 32;
  for (std::size_t dst = 0; dst < kBigLimbs; ++dst) {
    const std::size_t src = dst + limbs;
    if (src >= kBigLimbs) break;
    std::uint32_t low = value.limb[src] >> bits;
    if (bits != 0 && src + 1 < kBigLimbs) {
      low |= static_cast<std::uint32_t>(value.limb[src + 1] << (32 - bits));
    }
    out.limb[dst] = low;
  }
  value = out;
}

// Keeps bits [0, bits_kept) and clears the rest.
void big_mask_low(Big& value, int bits_kept) noexcept {
  for (std::size_t i = 0; i < kBigLimbs; ++i) {
    const int start = static_cast<int>(i) * 32;
    if (start >= bits_kept) {
      value.limb[i] = 0;
    } else if (start + 32 > bits_kept) {
      value.limb[i] &= (1U << (bits_kept - start)) - 1U;
    }
  }
}

int big_compare(const Big& lhs, const Big& rhs) noexcept {
  for (std::size_t i = kBigLimbs; i-- > 0;) {
    if (lhs.limb[i] != rhs.limb[i]) return lhs.limb[i] < rhs.limb[i] ? -1 : 1;
  }
  return 0;
}

// Requires lhs >= rhs.
void big_subtract(Big& lhs, const Big& rhs) noexcept {
  std::uint64_t borrow = 0;
  for (std::size_t i = 0; i < kBigLimbs; ++i) {
    const std::uint64_t subtrahend = static_cast<std::uint64_t>(rhs.limb[i]) + borrow;
    const std::uint64_t minuend = lhs.limb[i];
    if (minuend >= subtrahend) {
      lhs.limb[i] = static_cast<std::uint32_t>(minuend - subtrahend);
      borrow = 0;
    } else {
      lhs.limb[i] = static_cast<std::uint32_t>((minuend + (std::uint64_t{1} << 32)) - subtrahend);
      borrow = 1;
    }
  }
}

// Rounds value = q * 2^-p, plus a nonzero fraction below q's last bit when
// sticky is set, to `bits` significant bits with ties to even. The result is
// mantissa * 2^e2. min_e2 is the exponent of the smallest subnormal bit.
void round_binary(const Big& q, int p, bool sticky, int bits, int min_e2,
                  std::uint64_t& mantissa, int& e2) noexcept {
  int drop = big_bit_length(q) - bits;
  int exponent = drop - p;
  if (exponent < min_e2) {
    drop = p + min_e2;
    exponent = min_e2;
  }
  mantissa = 0;
  for (int i = 0; i < bits; ++i) {
    if (big_bit(q, drop + i)) mantissa |= std::uint64_t{1} << i;
  }
  const bool half = big_bit(q, drop - 1);
  const bool rest = sticky || big_any_below(q, drop - 1);
  if (half && (rest || (mantissa & 1U) != 0)) ++mantissa;
  if (mantissa == (std::uint64_t{1} << bits)) {
    mantissa >>= 1;
    ++exponent;
  }
  e2 = exponent;
}

// Rounds c * 10^exponent10 (c > 0) to `bits` significant bits, ties to even.
// Callers keep the magnitude bounded so the bignum never overflows.
void round_decimal(std::uint64_t c, int exponent10, int bits, int min_e2,
                   std::uint64_t& mantissa, int& e2) noexcept {
  Big numerator{};
  big_set(numerator, c);
  if (exponent10 >= 0) {
    for (int i = 0; i < exponent10; ++i) big_mul_add(numerator, 10, 0);
    round_binary(numerator, 0, false, bits, min_e2, mantissa, e2);
    return;
  }
  Big denominator{};
  big_set(denominator, 1);
  for (int i = 0; i < -exponent10; ++i) big_mul_add(denominator, 10, 0);
  // Scale so the quotient has bits+1 or bits+2 significant bits.
  const int p = bits + 1 + big_bit_length(denominator) - big_bit_length(numerator);
  if (p >= 0) {
    big_shift_left(numerator, p);
  } else {
    big_shift_left(denominator, -p);
  }
  Big quotient{};
  Big remainder = numerator;
  for (int k = bits + 1; k >= 0; --k) {
    Big shifted = denominator;
    big_shift_left(shifted, k);
    if (big_compare(remainder, shifted) >= 0) {
      big_subtract(remainder, shifted);
      quotient.limb[k / 32] |= 1U << (k % 32);
    }
  }
  round_binary(quotient, p, !big_is_zero(remainder), bits, min_e2, mantissa, e2);
}

int decimal_digit_count(std::uint64_t value) noexcept {
  int count = 1;
  for (; value >= 10; value /= 10) ++count;
  return count;
}

struct DecimalDigits {
  char digit[48];
  int count;
  int exponent10;  // value = 0.d0 d1 ... * 10^(exponent10 + 1)
  bool sticky;     // nonzero value beyond the stored digits
};

// Exact decimal digits of mantissa * 2^e2 (mantissa > 0). Integers yield every
// digit. Fractions yield the first ten significant digits and sticky.
void exact_decimal(std::uint32_t mantissa, int e2, DecimalDigits& out) noexcept {
  Big value{};
  big_set(value, mantissa);
  out.count = 0;
  out.exponent10 = 0;
  out.sticky = false;
  if (e2 >= 0) {
    big_shift_left(value, e2);
    char reversed[48];
    int n = 0;
    while (!big_is_zero(value)) {
      reversed[n++] = static_cast<char>('0' + big_div_small(value, 10));
    }
    for (int i = 0; i < n; ++i) out.digit[i] = reversed[n - 1 - i];
    out.count = n;
    out.exponent10 = n - 1;
    return;
  }
  const int k = -e2;
  Big fraction = value;
  big_mask_low(fraction, k);
  Big whole = value;
  big_shift_right(whole, k);
  bool started = false;
  if (!big_is_zero(whole)) {
    char reversed[12];
    int n = 0;
    while (!big_is_zero(whole)) {
      reversed[n++] = static_cast<char>('0' + big_div_small(whole, 10));
    }
    for (int i = 0; i < n; ++i) out.digit[out.count++] = reversed[n - 1 - i];
    out.exponent10 = n - 1;
    started = true;
  }
  int position = 0;
  while (out.count < 10 && !big_is_zero(fraction)) {
    big_mul_add(fraction, 10, 0);
    Big digit = fraction;
    big_shift_right(digit, k);
    const auto d = static_cast<std::uint8_t>(digit.limb[0]);
    big_mask_low(fraction, k);
    ++position;
    if (!started) {
      if (d == 0) continue;
      started = true;
      out.exponent10 = -position;
    }
    out.digit[out.count++] = static_cast<char>('0' + d);
  }
  out.sticky = !big_is_zero(fraction);
}

// Decimal text grammar: [ws]* [+-]? digits [. digits*] | . digits, then
// optional [eE][+-]?digits+. Hex, inf and nan are not decimal text.
struct DecimalText {
  bool negative = false;
  std::uint64_t digits = 0;  // significand without trailing zeros
  int exponent10 = 0;        // value = digits * 10^exponent10
};

bool is_c_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// Callers pass at most kSceneMaxFloatTextChars characters, so at most 15
// significand digits fit in 64 bits; the 18-digit guard is defensive.
bool parse_decimal_text(const char* text, std::size_t length, bool leading_space,
                        DecimalText& out) noexcept {
  std::size_t i = 0;
  if (leading_space) {
    while (i < length && is_c_space(text[i])) ++i;
  }
  bool negative = false;
  if (i < length && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }
  std::uint64_t digits = 0;
  int significant = 0;
  int fraction = 0;
  bool any_digit = false;
  bool point = false;
  for (; i < length; ++i) {
    const char c = text[i];
    if (c == '.' && !point) {
      point = true;
      continue;
    }
    if (c < '0' || c > '9') break;
    any_digit = true;
    if (point) ++fraction;
    if (digits == 0 && c == '0') continue;
    if (significant >= 18) return false;
    digits = digits * 10 + static_cast<std::uint64_t>(c - '0');
    ++significant;
  }
  if (!any_digit) return false;
  int exponent = 0;
  if (i < length && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    bool exponent_negative = false;
    if (i < length && (text[i] == '+' || text[i] == '-')) {
      exponent_negative = text[i] == '-';
      ++i;
    }
    if (i >= length || text[i] < '0' || text[i] > '9') return false;
    for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i) {
      if (exponent < 100000) exponent = exponent * 10 + (text[i] - '0');
    }
    if (exponent_negative) exponent = -exponent;
  }
  if (i != length) return false;
  int exponent10 = exponent - fraction;
  if (digits == 0) {
    exponent10 = 0;
  } else {
    while (digits % 10 == 0) {
      digits /= 10;
      ++exponent10;
    }
  }
  out.negative = negative;
  out.digits = digits;
  out.exponent10 = exponent10;
  return true;
}

// Double rounding, then binary32 rounding, matching newlib's strtof, which is
// (float)_strtod_l(). Values below 10^-45 round to zero in both steps. Values
// with magnitude above 10^40 are beyond FLT_MAX and overflow.
bool float_from_decimal(const DecimalText& d, std::uint32_t& bits) noexcept {
  const std::uint32_t sign_bit = d.negative ? 0x80000000U : 0U;
  if (d.digits == 0) {
    bits = sign_bit;
    return true;
  }
  const int magnitude = decimal_digit_count(d.digits) + d.exponent10;  // value < 10^magnitude
  if (magnitude > 40) return false;
  if (magnitude <= -46) {
    bits = sign_bit;
    return true;
  }
  std::uint64_t binary64 = 0;
  int e53 = 0;
  round_decimal(d.digits, d.exponent10, 53, -1074, binary64, e53);
  Big rounded;
  big_set(rounded, binary64);
  std::uint64_t binary32 = 0;
  int e24 = 0;
  round_binary(rounded, -e53, false, 24, -149, binary32, e24);
  if (binary32 == 0) {
    bits = sign_bit;
    return true;
  }
  if (binary32 < (std::uint64_t{1} << 23)) {  // subnormal: e24 == -149
    bits = sign_bit | static_cast<std::uint32_t>(binary32);
    return true;
  }
  const int field = e24 + 150;
  if (field > 254) return false;  // overflow to infinity
  bits = sign_bit | (static_cast<std::uint32_t>(field) << 23) |
         (static_cast<std::uint32_t>(binary32) & 0x7FFFFFU);
  return true;
}

// Truncates the exact value toward zero after one binary64 rounding, as the
// previous int(float(text)) / trunc(strtod(text)) path did.
bool integer_from_decimal(const DecimalText& d, std::int32_t& value) noexcept {
  if (d.digits == 0) {
    value = 0;
    return true;
  }
  const int magnitude = decimal_digit_count(d.digits) + d.exponent10;
  if (magnitude > 10) return false;  // at least 10^10 is outside int32
  if (magnitude <= -46) {
    value = 0;
    return true;
  }
  std::uint64_t binary64 = 0;
  int e = 0;
  round_decimal(d.digits, d.exponent10, 53, -1074, binary64, e);
  std::uint64_t magnitude_value = 0;
  if (binary64 != 0) {
    if (e >= 0) {
      int width = 0;
      for (std::uint64_t top = binary64; top != 0; top >>= 1) ++width;
      if (width + e > 33) return false;
      magnitude_value = binary64 << e;
    } else {
      magnitude_value = (-e >= 64) ? 0U : (binary64 >> (-e));
    }
  }
  if (d.negative) {
    if (magnitude_value > 2147483648ULL) return false;
    value = magnitude_value == 2147483648ULL
                ? INT32_MIN
                : -static_cast<std::int32_t>(magnitude_value);
  } else {
    if (magnitude_value > 2147483647ULL) return false;
    value = static_cast<std::int32_t>(magnitude_value);
  }
  return true;
}

// Exact equivalent of printf("%.9g") for a finite binary32 value.
bool format_float_text(float value, char* out, std::size_t capacity,
                       std::size_t& length) noexcept {
  length = 0;
  if (out == nullptr || !std::isfinite(value)) return false;
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const bool negative = (bits >> 31) != 0;
  const std::uint32_t exponent_field = (bits >> 23) & 0xFFU;
  const std::uint32_t fraction_field = bits & 0x7FFFFFU;
  char text[24];
  std::size_t n = 0;
  if (negative) text[n++] = '-';
  if (exponent_field == 0 && fraction_field == 0) {
    text[n++] = '0';
  } else {
    const bool subnormal = exponent_field == 0;
    const std::uint32_t mantissa = subnormal ? fraction_field : (fraction_field | (1U << 23));
    const int e2 = subnormal ? -149 : static_cast<int>(exponent_field) - 150;
    DecimalDigits decimal{};
    exact_decimal(mantissa, e2, decimal);
    int exponent10 = decimal.exponent10;
    int count = decimal.count;
    char digits[9];
    if (count > 9) {
      // Round the exact digits to nine significant digits, ties to even.
      const int round_digit = decimal.digit[9] - '0';
      bool rest = decimal.sticky;
      for (int i = 10; i < count; ++i) {
        if (decimal.digit[i] != '0') rest = true;
      }
      const bool odd = ((decimal.digit[8] - '0') & 1) != 0;
      const bool up = round_digit > 5 || (round_digit == 5 && (rest || odd));
      count = 9;
      if (up) {
        int i = 8;
        while (i >= 0 && decimal.digit[i] == '9') {
          decimal.digit[i] = '0';
          --i;
        }
        if (i >= 0) {
          ++decimal.digit[i];
        } else {
          decimal.digit[0] = '1';   // all nines carried out: 1 followed by zeros
          ++exponent10;
        }
      }
    }
    for (int i = 0; i < 9; ++i) digits[i] = i < count ? decimal.digit[i] : '0';
    if (exponent10 >= -4 && exponent10 < 9) {
      if (exponent10 >= 0) {
        for (int i = 0; i <= exponent10; ++i) text[n++] = digits[i];
        int last = 8;
        while (last > exponent10 && digits[last] == '0') --last;
        if (last > exponent10) {
          text[n++] = '.';
          for (int i = exponent10 + 1; i <= last; ++i) text[n++] = digits[i];
        }
      } else {
        text[n++] = '0';
        text[n++] = '.';
        for (int i = 0; i < -exponent10 - 1; ++i) text[n++] = '0';
        int last = 8;
        while (digits[last] == '0') --last;
        for (int i = 0; i <= last; ++i) text[n++] = digits[i];
      }
    } else {
      text[n++] = digits[0];
      int last = 8;
      while (last > 0 && digits[last] == '0') --last;
      if (last > 0) {
        text[n++] = '.';
        for (int i = 1; i <= last; ++i) text[n++] = digits[i];
      }
      text[n++] = 'e';
      text[n++] = exponent10 < 0 ? '-' : '+';
      const int magnitude = exponent10 < 0 ? -exponent10 : exponent10;
      text[n++] = static_cast<char>('0' + magnitude / 10);
      text[n++] = static_cast<char>('0' + magnitude % 10);
    }
  }
  if (n > kSceneMaxFloatTextChars || n + 1 > capacity) return false;
  std::memcpy(out, text, n);
  out[n] = '\0';
  length = n;
  return true;
}

bool format_integer_text(std::int32_t value, char* out, std::size_t capacity,
                         std::size_t& length) noexcept {
  length = 0;
  if (out == nullptr) return false;
  char reversed[12];
  std::size_t n = 0;
  const std::uint32_t magnitude =
      value < 0 ? 0U - static_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
  std::uint32_t remaining = magnitude;
  do {
    reversed[n++] = static_cast<char>('0' + remaining % 10U);
    remaining /= 10U;
  } while (remaining != 0);
  std::size_t total = n + (value < 0 ? 1U : 0U);
  if (total > kSceneMaxIntTextChars || total + 1 > capacity) return false;
  std::size_t position = 0;
  if (value < 0) out[position++] = '-';
  while (n > 0) out[position++] = reversed[--n];
  out[position] = '\0';
  length = position;
  return true;
}

struct Slice { const char* data; std::size_t size; };
struct Fields { Slice values[4]{}; std::uint8_t count = 0; };

constexpr ParamSpec kSetParams[] = {
    scene_integer_spec("key", 0, 0, 11),
    scene_enum_spec("scale", 1, 0, 20),
    scene_integer_spec("bpm", 120, 1, 300),
    scene_decimal_spec("swing", 0.0f, 0.0f, 1.0f),
    scene_enum_spec("transport_mode", 0, 0, 1),
};
constexpr ParamSpec kOutputParams[] = {
    scene_integer_spec("out_ch", 1, 1, 16),
    {"print", ParamType::Boolean, ParamValue::boolean(false),
     ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1),
     "print", nullptr, 0, ParamPersist},
};
constexpr SceneModuleSchema kSetSchema{"set", 1, SceneModuleKind::Fixed,
                                        kSetParams, 5, nullptr, 0};
constexpr SceneModuleSchema kOutputSchema{"output", 1, SceneModuleKind::Fixed,
                                           kOutputParams, 2, nullptr, 0};
constexpr SceneModuleSchema kSettingsSchema{"settings", 1, SceneModuleKind::Fixed,
                                             nullptr, 0, nullptr, 0};
constexpr SceneModuleSchema kNoteFallback{"note", 1, SceneModuleKind::Input,
                                           nullptr, 0, nullptr, 0};
constexpr SceneModuleSchema kEmptyFallback{"empty", 1, SceneModuleKind::Processor,
                                            nullptr, 0, nullptr, 0};

bool equal(Slice value, const char* text) noexcept {
  const std::size_t length = std::strlen(text);
  return value.size == length && std::memcmp(value.data, text, length) == 0;
}
bool equal(const char* lhs, const char* rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
}
bool copy_text(char* out, std::size_t capacity, Slice text) noexcept {
  if (text.size >= capacity) return false;
  std::memcpy(out, text.data, text.size);
  out[text.size] = '\0';
  return true;
}

Fields split_fields(Slice line) noexcept {
  Fields result{};
  std::size_t start = 0;
  for (std::size_t i = 0; i <= line.size; ++i) {
    if (i == line.size || line.data[i] == ',') {
      if (result.count == 4) return {};
      result.values[result.count++] = {line.data + start, i - start};
      start = i + 1;
    }
  }
  return result;
}

bool parse_i32(Slice text, std::int32_t& result) noexcept {
  if (text.size == 0) return false;
  std::size_t i = 0;
  bool negative = false;
  if (text.data[i] == '-' || text.data[i] == '+') {
    negative = text.data[i] == '-';
    if (++i == text.size) return false;
  }
  const std::uint32_t limit = negative ? 2147483648U : 2147483647U;
  std::uint32_t value = 0;
  for (; i < text.size; ++i) {
    const char c = text.data[i];
    if (c < '0' || c > '9') return false;
    const std::uint32_t digit = static_cast<std::uint32_t>(c - '0');
    if (value > (limit - digit) / 10U) return false;
    value = value * 10U + digit;
  }
  if (negative) {
    result = value == 2147483648U ? INT32_MIN : -static_cast<std::int32_t>(value);
  } else {
    result = static_cast<std::int32_t>(value);
  }
  return true;
}

// Binary32 text as the previous strtof path accepted it: leading ASCII space,
// sign, decimal digits with optional point and exponent, at most 15 characters.
// Hex, inf and nan are rejected. Trailing characters are rejected.
bool parse_float(Slice text, float& result) noexcept {
  return scene_parse_float(text.data, text.size, result);
}

// scenes.py accepts integer fields via int(float(text)). Keep exact int32
// strings first, then accept decimal/exponent notation truncated toward zero.
bool parse_integer_like(Slice text, std::int32_t& result) noexcept {
  if (parse_i32(text, result)) return true;
  return scene_parse_integer_like(text.data, text.size, result);
}

bool parse_boolean(Slice text, bool& result) noexcept {
  if (equal(text, "1") || equal(text, "True")) { result = true; return true; }
  if (equal(text, "0") || equal(text, "False")) { result = false; return true; }
  return false;
}

bool split_array(Slice text, Slice* values, std::size_t capacity,
                 std::uint16_t& count) noexcept {
  count = 0;
  if (text.size == 0) return true;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size; ++i) {
    if (i == text.size || text.data[i] == '|') {
      if (count >= capacity) return false;
      values[count++] = {text.data + start, i - start};
      start = i + 1;
    }
  }
  return true;
}

bool field_persisted(const ParamSpec& spec) noexcept {
  static constexpr const char* ui_names[] = {
      "active_scene", "save_scene", "copy_scene", "reset_scene", "key_brightness"};
  if ((spec.flags & ParamPersist) == 0 || (spec.flags & ParamUiOnly) != 0 ||
      spec.type == ParamType::Action || spec.name == nullptr)
    return false;
  for (const auto* name : ui_names) if (equal(spec.name, name)) return false;
  return true;
}

const ParamSpec* find_param(const SceneModuleSchema& schema, Slice name) noexcept {
  if (schema.params == nullptr) return nullptr;
  for (std::uint8_t i = 0; i < schema.param_count; ++i) {
    const auto& spec = schema.params[i];
    if (field_persisted(spec) && equal(name, spec.name)) return &spec;
  }
  return nullptr;
}
const SceneAttributeSpec* find_attr(const SceneModuleSchema& schema, Slice name) noexcept {
  if (schema.attrs == nullptr) return nullptr;
  for (std::uint8_t i = 0; i < schema.attr_count; ++i) {
    const auto& spec = schema.attrs[i];
    if (spec.name != nullptr && equal(name, spec.name)) return &spec;
  }
  return nullptr;
}

SceneModuleKind expected_kind(std::uint8_t slot) noexcept {
  if (slot == 1) return SceneModuleKind::Input;
  if (slot >= 2 && slot <= 7) return SceneModuleKind::Processor;
  return SceneModuleKind::Fixed;
}

bool resolve_schema(std::uint8_t slot, const char* id, const SceneSchemaResolver& resolver,
                    SceneModuleSchema& schema) noexcept {
  if (const auto* fixed = fixed_scene_schema(slot, id)) {
    schema = *fixed;
    return true;
  }
  return resolver.resolve(slot, id, schema) && schema.id != nullptr &&
         schema.kind == expected_kind(slot);
}

const SceneModuleSchema* fallback_schema(std::uint8_t slot) noexcept {
  if (slot == 1) return &kNoteFallback;
  if (slot >= 2 && slot <= 7) return &kEmptyFallback;
  if (slot == 0) return &kSetSchema;
  if (slot == 8) return &kOutputSchema;
  if (slot == 9) return &kSettingsSchema;
  return nullptr;
}

SceneResult check_schema(const SceneModuleSchema& schema) noexcept {
  if (schema.id == nullptr || schema.param_count > 64 || schema.attr_count > 64)
    return SceneResult::InvalidSchema;
  if ((schema.param_count != 0 && schema.params == nullptr) ||
      (schema.attr_count != 0 && schema.attrs == nullptr)) return SceneResult::InvalidSchema;
  std::size_t params = 0;
  for (std::uint8_t i = 0; i < schema.param_count; ++i) {
    if (field_persisted(schema.params[i])) {
      ++params;
      if (schema.params[i].type == ParamType::Text) return SceneResult::InvalidSchema;
    }
  }
  if (params > kSceneMaxParamsPerModule || schema.attr_count > kSceneMaxAttrsPerModule)
    return SceneResult::CapacityExceeded;
  for (std::uint8_t i = 0; i < schema.attr_count; ++i) {
    const auto& attr = schema.attrs[i];
    if (attr.name == nullptr || attr.element_count > kSceneMaxAttrValues)
      return SceneResult::CapacityExceeded;
  }
  return SceneResult::Ok;
}

bool check_param_value(const ParamSpec& spec, SceneValueKind& kind,
                       SceneScalar& value) noexcept {
  switch (spec.type) {
    case ParamType::Integer:
    case ParamType::Enumeration: {
      if (spec.minimum.type != ParamType::Integer || spec.maximum.type != ParamType::Integer)
        return false;
      if (value.integer < spec.minimum.value.integer || value.integer > spec.maximum.value.integer)
        return false;
      kind = SceneValueKind::Integer;
      return true;
    }
    case ParamType::Boolean:
      return false;
    case ParamType::Decimal:
      if (spec.minimum.type != ParamType::Decimal || spec.maximum.type != ParamType::Decimal)
        return false;
      if (!std::isfinite(value.decimal) || value.decimal < spec.minimum.value.decimal ||
          value.decimal > spec.maximum.value.decimal) return false;
      kind = SceneValueKind::Decimal;
      return true;
    case ParamType::Text:
      return false;
    case ParamType::Action:
      return false;
  }
  return false;
}

SceneResult parse_param(const ParamSpec& spec, Slice text,
                        SceneParameter& result) noexcept {
  if (!copy_text(result.name, sizeof(result.name), {spec.name, std::strlen(spec.name)}))
    return SceneResult::InvalidSchema;
  SceneValueKind kind{};
  SceneScalar scalar{};
  switch (spec.type) {
    case ParamType::Integer:
    case ParamType::Enumeration:
      if (!parse_integer_like(text, scalar.integer)) return SceneResult::InvalidValue;
      break;
    case ParamType::Boolean: {
      if (!parse_boolean(text, scalar.boolean)) return SceneResult::InvalidValue;
      kind = SceneValueKind::Boolean;
      result.kind = kind;
      result.value = scalar;
      return SceneResult::Ok;
    }
    case ParamType::Decimal:
      if (!parse_float(text, scalar.decimal)) return SceneResult::InvalidValue;
      break;
    case ParamType::Text:
      return SceneResult::InvalidSchema; // No source module persists StringParmType.
    case ParamType::Action:
      return SceneResult::InvalidSchema;
  }
  if (!check_param_value(spec, kind, scalar)) return SceneResult::InvalidValue;
  result.kind = kind;
  result.value = scalar;
  return SceneResult::Ok;
}

SceneResult parse_attribute(const SceneAttributeSpec& spec, Slice text,
                            SceneAttribute& result) noexcept {
  if (!copy_text(result.name, sizeof(result.name), {spec.name, std::strlen(spec.name)}))
    return SceneResult::InvalidSchema;
  Slice parts[kSceneMaxAttrValues]{};
  std::uint16_t count = 0;
  if (!split_array(text, parts, kSceneMaxAttrValues, count) || count != spec.element_count)
    return SceneResult::CapacityExceeded;
  result.type = spec.type;
  result.count = count;
  for (std::uint16_t i = 0; i < count; ++i) {
    switch (spec.type) {
      case AttributeType::Integer:
        if (!parse_integer_like(parts[i], result.values[i].integer)) return SceneResult::InvalidValue;
        break;
      case AttributeType::Decimal:
        if (!parse_float(parts[i], result.values[i].decimal)) return SceneResult::InvalidValue;
        break;
      case AttributeType::Boolean: {
        if (!parse_boolean(parts[i], result.values[i].boolean)) return SceneResult::InvalidValue;
        break;
      }
      case AttributeType::Byte: {
        std::int32_t value = 0;
        if (!parse_integer_like(parts[i], value) || value < 0 || value > 255) return SceneResult::InvalidValue;
        result.values[i].integer = value;
        break;
      }
    }
  }
  return SceneResult::Ok;
}

SceneParameter* find_stored_param(SceneModule& module, const char* name) noexcept {
  for (std::uint8_t i = 0; i < module.parameter_count; ++i)
    if (equal(module.parameters[i].name, name)) return &module.parameters[i];
  if (module.parameter_count == kSceneMaxParamsPerModule) return nullptr;
  return &module.parameters[module.parameter_count++];
}
SceneAttribute* find_stored_attr(SceneModule& module, const char* name) noexcept {
  for (std::uint8_t i = 0; i < module.attribute_count; ++i)
    if (equal(module.attributes[i].name, name)) return &module.attributes[i];
  if (module.attribute_count == kSceneMaxAttrsPerModule) return nullptr;
  return &module.attributes[module.attribute_count++];
}

struct Writer {
  SceneWriter port;
  std::size_t size = 0;
  SceneResult failure = SceneResult::TooLarge;
  bool append(const char* text, std::size_t length) noexcept {
    if (length > kSceneMaxBytes - size) { failure = SceneResult::TooLarge; return false; }
    if (port.write == nullptr) { failure = SceneResult::IoError; return false; }
    const auto status = port.write(port.context, text, length);
    if (status != SceneWriteStatus::Written) {
      failure = status == SceneWriteStatus::Full ? SceneResult::TooLarge : SceneResult::IoError;
      return false;
    }
    size += length;
    return true;
  }
  bool append(const char* text) noexcept { return append(text, std::strlen(text)); }
  bool line_end() noexcept { return append("\n", 1); }
};

bool write_integer(Writer& writer, std::int32_t value) noexcept {
  char text[kSceneMaxIntTextChars + 1]{};
  std::size_t length = 0;
  return format_integer_text(value, text, sizeof(text), length) &&
         writer.append(text, length);
}
bool write_float(Writer& writer, float value) noexcept {
  char text[kSceneMaxFloatTextChars + 1]{};
  std::size_t length = 0;
  return format_float_text(value, text, sizeof(text), length) &&
         writer.append(text, length);
}
bool write_scalar(Writer& writer, SceneValueKind kind, const SceneScalar& value) noexcept {
  switch (kind) {
    case SceneValueKind::Integer: return write_integer(writer, value.integer);
    case SceneValueKind::Decimal: return write_float(writer, value.decimal);
    case SceneValueKind::Boolean: return write_integer(writer, value.boolean ? 1 : 0);
  }
  return false;
}

bool validate_stored_param(const ParamSpec& spec, const SceneParameter& param) noexcept {
  switch (spec.type) {
    case ParamType::Integer:
    case ParamType::Enumeration:
      return param.kind == SceneValueKind::Integer &&
             spec.minimum.type == ParamType::Integer && spec.maximum.type == ParamType::Integer &&
             param.value.integer >= spec.minimum.value.integer &&
             param.value.integer <= spec.maximum.value.integer;
    case ParamType::Boolean:
      return param.kind == SceneValueKind::Boolean;
    case ParamType::Decimal:
      return param.kind == SceneValueKind::Decimal &&
             spec.minimum.type == ParamType::Decimal && spec.maximum.type == ParamType::Decimal &&
             std::isfinite(param.value.decimal) && param.value.decimal >= spec.minimum.value.decimal &&
             param.value.decimal <= spec.maximum.value.decimal;
    case ParamType::Text:
    case ParamType::Action:
      return false;
  }
  return false;
}

bool write_global(Writer& writer, const char* name, std::int32_t value) noexcept {
  return writer.append("S,") && writer.append(name) && writer.append(",") &&
         write_integer(writer, value) && writer.line_end();
}
bool write_global_float(Writer& writer, const char* name, float value) noexcept {
  return writer.append("S,") && writer.append(name) && writer.append(",") &&
         write_float(writer, value) && writer.line_end();
}

SceneResult parse_global(Slice name, Slice value, SceneDto& scene) noexcept {
  if (!equal(name, "key") && !equal(name, "scale") && !equal(name, "octave") &&
      !equal(name, "key_offset") && !equal(name, "bpm") && !equal(name, "swing") &&
      !equal(name, "transport_mode")) return SceneResult::Ok;
  std::int32_t integer = 0;
  if (equal(name, "swing")) {
    float decimal = 0;
    if (!parse_float(value, decimal) || decimal < 0.0f || decimal > 1.0f)
      return SceneResult::InvalidValue;
    scene.globals.swing = decimal;
    scene.global_fields |= SceneSwing;
    return SceneResult::Ok;
  }
  if (!parse_integer_like(value, integer)) return SceneResult::InvalidValue;
  if (equal(name, "key")) {
    if (integer < 0 || integer >= static_cast<std::int32_t>(kKeys)) return SceneResult::InvalidValue;
    scene.globals.key = static_cast<std::uint8_t>(integer); scene.global_fields |= SceneKey;
  } else if (equal(name, "scale")) {
    if (integer < 0 || integer >= static_cast<std::int32_t>(kScaleCount)) return SceneResult::InvalidValue;
    scene.globals.scale = static_cast<std::uint8_t>(integer); scene.global_fields |= SceneScale;
  } else if (equal(name, "octave")) {
    scene.globals.octave = integer; scene.global_fields |= SceneOctave;
  } else if (equal(name, "key_offset")) {
    scene.globals.key_offset = integer; scene.global_fields |= SceneKeyOffset;
  } else if (equal(name, "bpm")) {
    if (integer < 1 || integer > 300) return SceneResult::InvalidValue;
    scene.globals.bpm = static_cast<std::uint16_t>(integer); scene.global_fields |= SceneBpm;
  } else if (equal(name, "transport_mode")) {
    if (integer < 0 || integer > 1) return SceneResult::InvalidValue;
    scene.globals.transport_mode = static_cast<TransportMode>(integer);
    scene.global_fields |= SceneTransportMode;
  }
  return SceneResult::Ok; // Unknown S names are intentionally ignored.
}

}  // namespace

bool scene_format_float(float value, char* out, std::size_t capacity,
                        std::size_t& length) noexcept {
  return format_float_text(value, out, capacity, length);
}

bool scene_parse_float(const char* text, std::size_t length, float& value) noexcept {
  if (text == nullptr || length == 0 || length > kSceneMaxFloatTextChars) return false;
  DecimalText decimal;
  if (!parse_decimal_text(text, length, true, decimal)) return false;
  std::uint32_t bits = 0;
  if (!float_from_decimal(decimal, bits)) return false;
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool scene_parse_integer_like(const char* text, std::size_t length,
                              std::int32_t& value) noexcept {
  if (text == nullptr || length == 0 || length > kSceneMaxFloatTextChars) return false;
  DecimalText decimal;
  if (!parse_decimal_text(text, length, false, decimal)) return false;
  return integer_from_decimal(decimal, value);
}

const SceneModuleSchema* fixed_scene_schema(std::uint8_t slot, const char* id) noexcept {
  if (slot == 0 && equal(id, "set")) return &kSetSchema;
  if (slot == 8 && equal(id, "output")) return &kOutputSchema;
  if (slot == 9 && equal(id, "settings")) return &kSettingsSchema;
  return nullptr;
}

// Input bank helpers. Bank slots: the layout index of an input id, and the ordinal of a persisted
// parameter among the schema's persisted parameters (the bit used in SceneInputBankMeta).
constexpr std::uint8_t kSceneInputSlot = 1;  // SET 0, IN 1, processors 2..7, OUT 8, SETTINGS 9
std::size_t scene_input_index(const char* id) noexcept {
  for (std::size_t i = 0; i < kSceneInputBanks; ++i) {
    if (equal(id, kSceneInputLayout[i].id)) return i;
  }
  return kSceneInputBanks;
}
std::size_t persisted_ordinal(const SceneModuleSchema& schema, const ParamSpec* spec) noexcept {
  std::size_t ordinal = 0;
  for (std::uint8_t i = 0; i < schema.param_count; ++i) {
    if (&schema.params[i] == spec) return ordinal;
    if (field_persisted(schema.params[i])) ++ordinal;
  }
  return ordinal;
}
std::size_t persisted_count(const SceneModuleSchema& schema) noexcept {
  std::size_t count = 0;
  for (std::uint8_t i = 0; i < schema.param_count; ++i) if (field_persisted(schema.params[i])) ++count;
  return count;
}
std::size_t attribute_values(const SceneModuleSchema& schema) noexcept {
  std::size_t count = 0;
  for (std::uint8_t i = 0; i < schema.attr_count; ++i) count += schema.attrs[i].element_count;
  return count;
}
// Scalar index of attribute j's first element inside the region of a layout.
std::size_t attribute_base(const SceneInputLayout& layout, const SceneModuleSchema& schema,
                           std::uint8_t j) noexcept {
  std::size_t base = layout.offset + layout.parameters;
  for (std::uint8_t k = 0; k < j; ++k) base += schema.attrs[k].element_count;
  return base;
}

SceneResult decode_scene(SceneReader reader, const SceneSchemaResolver& resolver,
                         SceneDto& staged) noexcept {
  // Aggregate assignment of a whole DTO materializes a >10 KiB temporary with
  // this pinned Cortex-M0+ compiler, exceeding the core's 8 KiB stack.
  staged.valid = false;
  staged.globals = GlobalState{};
  staged.global_fields = 0;
  staged.diagnostics = SceneDiagnosticNone;
  for (auto& module : staged.modules) module = SceneModule{};
  staged.banks.present = 0;
  for (auto& meta : staged.banks.meta) meta = SceneInputBankMeta{};
  if (reader.read == nullptr) return SceneResult::IoError;
  std::size_t total_bytes = 0;
  bool first_line = true;
  bool ended = false;
  bool have_current = false;
  std::uint8_t current_slot = 0;
  SceneModuleSchema current_schema{};
  bool have_bank = false;
  std::size_t bank_index = 0;
  SceneModuleSchema bank_schema{};
  for (;;) {
    char line_buffer[kSceneMaxLineChars + 2]{};
    std::size_t line_length = 0;
    bool saw_byte = false;
    bool line_end = false;
    while (!line_end) {
      char byte = 0;
      const auto read_status = reader.read(reader.context, byte);
      if (read_status == SceneReadStatus::Missing) return SceneResult::Missing;
      if (read_status == SceneReadStatus::Error) return SceneResult::IoError;
      if (read_status == SceneReadStatus::End) break;
      saw_byte = true;
      if (++total_bytes > kSceneMaxBytes) return SceneResult::TooLarge;
      if (byte == '\n') { line_end = true; break; }
      if (line_length >= kSceneMaxLineChars + 1) return SceneResult::TooLarge;
      line_buffer[line_length++] = byte;
    }
    if (!saw_byte && !line_end) break;
    if (line_length != 0 && line_buffer[line_length - 1] == '\r') --line_length;
    if (line_length > kSceneMaxLineChars) return SceneResult::TooLarge;
    Slice line{line_buffer, line_length};
    if (first_line) {
      first_line = false;
      if (!equal(line, "SCN1")) return SceneResult::Unsupported;
      continue;
    }
    if (line.size == 0) continue;
    if (equal(line, "END")) { ended = true; break; }
    const auto fields = split_fields(line);
    if (fields.count == 0) return SceneResult::Malformed;
    if (equal(fields.values[0], "S")) {
      if (fields.count != 3) return SceneResult::Malformed;
      const auto status = parse_global(fields.values[1], fields.values[2], staged);
      if (status != SceneResult::Ok) return status;
    } else if (equal(fields.values[0], "M")) {
      if (fields.count != 4) return SceneResult::Malformed;
      std::int32_t slot_value = 0, version_value = 0;
      if (!parse_i32(fields.values[1], slot_value) || slot_value < 0 || slot_value >= 10 ||
          !parse_i32(fields.values[3], version_value) || version_value < 0 || version_value > UINT16_MAX)
        return SceneResult::InvalidValue;
      char id[kSceneMaxModuleIdChars + 1]{};
      const bool id_fits = copy_text(id, sizeof(id), fields.values[2]);
      std::uint8_t slot = static_cast<std::uint8_t>(slot_value);
      if (slot == 0 && (!id_fits || !equal(id, "set"))) slot = 1;
      else if (slot == 1 && id_fits && equal(id, "transport")) { slot = 0; std::memcpy(id, "set", 4); }
      if (staged.modules[slot].present) return SceneResult::Malformed;
      SceneModuleSchema found{};
      bool known = id_fits && resolve_schema(slot, id, resolver, found);
      if (!known) {
        const auto* fallback = fallback_schema(slot);
        if (fallback == nullptr) return SceneResult::Malformed;
        found = *fallback;
        staged.diagnostics |= SceneUnknownModule;
      } else if (version_value > found.version) {
        return SceneResult::Unsupported;
      }
      const auto schema_status = check_schema(found);
      if (schema_status != SceneResult::Ok) return schema_status;
      auto& module = staged.modules[slot];
      module.present = true;
      module.unknown_module = !known;
      module.version = known ? static_cast<std::uint16_t>(version_value) : found.version;
      if (!copy_text(module.id, sizeof(module.id), {found.id, std::strlen(found.id)}))
        return SceneResult::InvalidSchema;
      current_slot = slot;
      current_schema = found;
      have_current = true;
    } else if (equal(fields.values[0], "P")) {
      if (fields.count != 3) return SceneResult::Malformed;
      if (!have_current) continue;
      const auto* spec = find_param(current_schema, fields.values[1]);
      if (spec == nullptr) continue; // Source-compatible unknown parameter tolerance.
      if (std::strlen(spec->name) > kSceneMaxFieldNameChars) return SceneResult::InvalidSchema;
      auto* stored = find_stored_param(staged.modules[current_slot], spec->name);
      if (stored == nullptr) return SceneResult::CapacityExceeded;
      const auto status = parse_param(*spec, fields.values[2], *stored);
      if (status != SceneResult::Ok) return status;
    } else if (equal(fields.values[0], "A")) {
      if (fields.count != 3) return SceneResult::Malformed;
      if (!have_current) continue;
      const auto* spec = find_attr(current_schema, fields.values[1]);
      if (spec == nullptr) continue;
      if (std::strlen(spec->name) > kSceneMaxAttrNameChars) return SceneResult::InvalidSchema;
      auto* stored = find_stored_attr(staged.modules[current_slot], spec->name);
      if (stored == nullptr) return SceneResult::CapacityExceeded;
      const auto status = parse_attribute(*spec, fields.values[2], *stored);
      if (status != SceneResult::Ok) return status;
    } else if (equal(fields.values[0], "BANK")) {
      // Input edit bank. Unknown inputs are tolerated: their BP/BA lines are skipped.
      if (fields.count != 2) return SceneResult::Malformed;
      have_bank = false;
      char id[kSceneMaxModuleIdChars + 1]{};
      if (!copy_text(id, sizeof(id), fields.values[1])) continue;
      const std::size_t index = scene_input_index(id);
      if (index == kSceneInputBanks) continue;
      if (((staged.banks.present >> index) & 1U) != 0) return SceneResult::Malformed;
      SceneModuleSchema found{};
      if (!resolve_schema(kSceneInputSlot, id, resolver, found)) return SceneResult::InvalidSchema;
      const auto schema_status = check_schema(found);
      if (schema_status != SceneResult::Ok) return schema_status;
      const auto& layout = kSceneInputLayout[index];
      if (persisted_count(found) > layout.parameters || attribute_values(found) != layout.attribute_values)
        return SceneResult::InvalidSchema;
      staged.banks.present = static_cast<std::uint8_t>(staged.banks.present | (1U << index));
      have_bank = true;
      bank_index = index;
      bank_schema = found;
    } else if (equal(fields.values[0], "BP")) {
      if (fields.count != 3) return SceneResult::Malformed;
      if (!have_bank) continue;
      const auto* spec = find_param(bank_schema, fields.values[1]);
      if (spec == nullptr || !field_persisted(*spec)) continue;  // unknown names are tolerated
      SceneParameter parsed{};
      const auto status = parse_param(*spec, fields.values[2], parsed);
      if (status != SceneResult::Ok) return status;
      const auto& layout = kSceneInputLayout[bank_index];
      const std::size_t ordinal = persisted_ordinal(bank_schema, spec);
      staged.banks.values[layout.offset + ordinal] = parsed.value;
      staged.banks.meta[bank_index].parameters |= 1U << ordinal;
    } else if (equal(fields.values[0], "BA")) {
      if (fields.count != 3) return SceneResult::Malformed;
      if (!have_bank) continue;
      const auto* spec = find_attr(bank_schema, fields.values[1]);
      if (spec == nullptr) continue;
      const auto j = static_cast<std::uint8_t>(spec - bank_schema.attrs);
      SceneAttribute parsed{};
      const auto status = parse_attribute(*spec, fields.values[2], parsed);
      if (status != SceneResult::Ok) return status;
      const std::size_t base = attribute_base(kSceneInputLayout[bank_index], bank_schema, j);
      for (std::uint16_t e = 0; e < parsed.count; ++e) staged.banks.values[base + e] = parsed.values[e];
      staged.banks.meta[bank_index].attributes = static_cast<std::uint8_t>(
          staged.banks.meta[bank_index].attributes | (1U << j));
    }
  }
  if (first_line || !ended) return SceneResult::Malformed;

  staged.valid = true;
  return SceneResult::Ok;
}

struct MemoryReader { const char* data; std::size_t length; std::size_t position; };
SceneReadStatus memory_read(void* context, char& value) noexcept {
  auto& reader = *static_cast<MemoryReader*>(context);
  if (reader.data == nullptr) return SceneReadStatus::Missing;
  if (reader.position >= reader.length) return SceneReadStatus::End;
  value = reader.data[reader.position++];
  return SceneReadStatus::Byte;
}

SceneResult decode_scene(const char* data, std::size_t length,
                         const SceneSchemaResolver& resolver, SceneDto& staging) noexcept {
  staging.valid = false;
  if (length > kSceneMaxBytes) return SceneResult::TooLarge;
  MemoryReader memory{data, length, 0};
  return decode_scene({&memory, memory_read}, resolver, staging);
}

SceneResult encode_scene(const SceneDto& scene, const SceneSchemaResolver& resolver,
                         SceneWriter output, std::size_t& written) noexcept {
  written = 0;
  if (!scene.valid) return SceneResult::InvalidValue;
  if (output.write == nullptr) return SceneResult::IoError;
  Writer writer{output};
  if (!writer.append("SCN1\n", 5)) return writer.failure;
  const auto& globals = scene.globals;
  if ((scene.global_fields & SceneKey) && !write_global(writer, "key", globals.key)) return writer.failure;
  if ((scene.global_fields & SceneScale) && !write_global(writer, "scale", globals.scale)) return writer.failure;
  if ((scene.global_fields & SceneOctave) && !write_global(writer, "octave", globals.octave)) return writer.failure;
  if ((scene.global_fields & SceneKeyOffset) && !write_global(writer, "key_offset", globals.key_offset)) return writer.failure;
  if ((scene.global_fields & SceneBpm) && !write_global(writer, "bpm", globals.bpm)) return writer.failure;
  if ((scene.global_fields & SceneSwing) && !write_global_float(writer, "swing", globals.swing)) return writer.failure;
  if ((scene.global_fields & SceneTransportMode) &&
      !write_global(writer, "transport_mode", static_cast<std::int32_t>(globals.transport_mode)))
    return writer.failure;

  for (std::uint8_t slot = 0; slot < kSceneModuleSlots; ++slot) {
    const auto& module = scene.modules[slot];
    if (!module.present) continue;
    if (module.parameter_count > kSceneMaxParamsPerModule ||
        module.attribute_count > kSceneMaxAttrsPerModule ||
        std::strlen(module.id) > kSceneMaxModuleIdChars) return SceneResult::CapacityExceeded;
    SceneModuleSchema schema{};
    if (!resolve_schema(slot, module.id, resolver, schema)) return SceneResult::InvalidSchema;
    const auto schema_status = check_schema(schema);
    if (schema_status != SceneResult::Ok) return schema_status;
    if (!writer.append("M,") || !write_integer(writer, slot) || !writer.append(",") ||
        !writer.append(module.id) || !writer.append(",") || !write_integer(writer, module.version) ||
        !writer.line_end()) return writer.failure;
    for (std::uint8_t i = 0; i < module.parameter_count; ++i) {
      const auto& param = module.parameters[i];
      Slice name{param.name, std::strlen(param.name)};
      const auto* spec = find_param(schema, name);
      if (spec == nullptr || !validate_stored_param(*spec, param)) return SceneResult::InvalidSchema;
      if (!writer.append("P,") || !writer.append(param.name) || !writer.append(",") ||
          !write_scalar(writer, param.kind, param.value) || !writer.line_end())
        return writer.failure;
    }
    for (std::uint8_t i = 0; i < module.attribute_count; ++i) {
      const auto& attr = module.attributes[i];
      const auto* spec = find_attr(schema, {attr.name, std::strlen(attr.name)});
      if (spec == nullptr || attr.type != spec->type || attr.count != spec->element_count ||
          attr.count > kSceneMaxAttrValues)
        return SceneResult::InvalidSchema;
      if (!writer.append("A,") || !writer.append(attr.name) || !writer.append(","))
        return writer.failure;
      for (std::uint16_t value = 0; value < attr.count; ++value) {
        if (value != 0 && !writer.append("|")) return writer.failure;
        SceneValueKind kind = SceneValueKind::Integer;
        if (attr.type == AttributeType::Decimal) kind = SceneValueKind::Decimal;
        else if (attr.type == AttributeType::Boolean) kind = SceneValueKind::Boolean;
        if (!write_scalar(writer, kind, attr.values[value])) return writer.failure;
      }
      if (!writer.line_end()) return writer.failure;
    }
  }
  for (std::size_t index = 0; index < kSceneInputBanks; ++index) {
    if (((scene.banks.present >> index) & 1U) == 0) continue;
    const auto& layout = kSceneInputLayout[index];
    SceneModuleSchema schema{};
    if (!resolve_schema(kSceneInputSlot, layout.id, resolver, schema)) return SceneResult::InvalidSchema;
    const auto schema_status = check_schema(schema);
    if (schema_status != SceneResult::Ok) return schema_status;
    if (persisted_count(schema) > layout.parameters || attribute_values(schema) != layout.attribute_values)
      return SceneResult::InvalidSchema;
    const auto& meta = scene.banks.meta[index];
    if (!writer.append("BANK,") || !writer.append(layout.id) || !writer.line_end()) return writer.failure;
    std::size_t ordinal = 0;
    for (std::uint8_t i = 0; i < schema.param_count; ++i) {
      const auto& spec = schema.params[i];
      if (!field_persisted(spec)) continue;
      if (((meta.parameters >> ordinal) & 1U) != 0) {
        if (!writer.append("BP,") || !writer.append(spec.name) || !writer.append(",") ||
            !write_scalar(writer, scene_param_value_kind(spec), scene.banks.values[layout.offset + ordinal]) ||
            !writer.line_end()) return writer.failure;
      }
      ++ordinal;
    }
    for (std::uint8_t j = 0; j < schema.attr_count; ++j) {
      if (((meta.attributes >> j) & 1U) == 0) continue;
      const auto& spec = schema.attrs[j];
      if (spec.element_count > kSceneMaxAttrValues) return SceneResult::InvalidSchema;
      const std::size_t base = attribute_base(layout, schema, j);
      SceneValueKind kind = SceneValueKind::Integer;
      if (spec.type == AttributeType::Decimal) kind = SceneValueKind::Decimal;
      else if (spec.type == AttributeType::Boolean) kind = SceneValueKind::Boolean;
      if (!writer.append("BA,") || !writer.append(spec.name) || !writer.append(",")) return writer.failure;
      for (std::uint16_t e = 0; e < spec.element_count; ++e) {
        if (e != 0 && !writer.append("|")) return writer.failure;
        if (!write_scalar(writer, kind, scene.banks.values[base + e])) return writer.failure;
      }
      if (!writer.line_end()) return writer.failure;
    }
  }
  if (!writer.append("END\n", 4)) return writer.failure;
  if (writer.size > kSceneMaxBytes) return writer.failure;
  written = writer.size;
  return SceneResult::Ok;
}

struct MemoryWriter { char* data; std::size_t capacity; std::size_t position; };
SceneWriteStatus memory_write(void* context, const char* data, std::size_t length) noexcept {
  auto& writer = *static_cast<MemoryWriter*>(context);
  if (length > writer.capacity - writer.position) return SceneWriteStatus::Full;
  std::memcpy(writer.data + writer.position, data, length);
  writer.position += length;
  return SceneWriteStatus::Written;
}

SceneResult encode_scene(const SceneDto& scene, const SceneSchemaResolver& resolver,
                         char* output, std::size_t capacity,
                         std::size_t& written) noexcept {
  if (output == nullptr) { written = 0; return SceneResult::IoError; }
  MemoryWriter memory{output, capacity, 0};
  return encode_scene(scene, resolver, {&memory, memory_write}, written);
}

}  // namespace orbit

namespace orbit {
bool scene_param_persisted(const ParamSpec& spec) noexcept { return field_persisted(spec); }
SceneValueKind scene_param_value_kind(const ParamSpec& spec) noexcept {
  if (spec.type == ParamType::Boolean) return SceneValueKind::Boolean;
  if (spec.type == ParamType::Decimal) return SceneValueKind::Decimal;
  return SceneValueKind::Integer;
}
}  // namespace orbit
namespace orbit {
std::size_t scene_bank_index(const char* id) noexcept { return scene_input_index(id); }
std::size_t scene_bank_attribute_base(const SceneInputLayout& layout, const SceneModuleSchema& schema,
                                      std::uint8_t j) noexcept {
  return attribute_base(layout, schema, j);
}
}  // namespace orbit
