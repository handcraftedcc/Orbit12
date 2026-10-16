#include "user_settings.hpp"

#include <cstring>

namespace orbit {
namespace {

constexpr char kKeyName[] = "key_brightness";
constexpr std::size_t kKeyNameLength = sizeof(kKeyName) - 1;
constexpr std::uint64_t kMicroOne = 1000000ULL;

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }
bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

// Parses Python float text: [+-] digits [. digits] [(e|E) [+-] digits] and returns the
// value in micro-units (1e-6), rounded half up. Python may write unrounded binary
// noise ("0.30000000000000004", "2.7755575615628914e-17") and "-0.0"; those are
// accepted. Values outside 0..1 after rounding are rejected, never clamped.
// Non-finite text, hex, and stray characters are malformed.
bool parse_micro(const char* text, std::size_t length, std::uint64_t& micro,
                 UserSettingsResult& failure) noexcept {
  std::size_t i = 0;
  bool negative = false;
  if (i < length && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }
  constexpr std::uint64_t kDigitCapacity = 100000000000000000ULL;  // 1e17: keeps 18 digits
  std::uint64_t digits = 0;
  int scale = 0;   // value = digits * 10^scale
  bool any_digit = false;
  bool integer_overflow = false;
  while (i < length && is_digit(text[i])) {
    any_digit = true;
    if (digits < kDigitCapacity) digits = digits * 10 + static_cast<std::uint64_t>(text[i] - '0');
    else integer_overflow = true;
    ++i;
  }
  if (i < length && text[i] == '.') {
    ++i;
    while (i < length && is_digit(text[i])) {
      any_digit = true;
      // Fraction digits beyond the capacity cannot change a micro-unit result.
      if (digits < kDigitCapacity) {
        digits = digits * 10 + static_cast<std::uint64_t>(text[i] - '0');
        --scale;
      }
      ++i;
    }
  }
  if (!any_digit) {
    failure = UserSettingsResult::Malformed;
    return false;
  }
  if (i < length && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    bool exponent_negative = false;
    if (i < length && (text[i] == '+' || text[i] == '-')) {
      exponent_negative = text[i] == '-';
      ++i;
    }
    if (i >= length || !is_digit(text[i])) {
      failure = UserSettingsResult::Malformed;
      return false;
    }
    int exponent = 0;
    while (i < length && is_digit(text[i])) {
      if (exponent < 100000) exponent = exponent * 10 + (text[i] - '0');
      ++i;
    }
    scale += exponent_negative ? -exponent : exponent;
  }
  if (i != length) {
    failure = UserSettingsResult::Malformed;
    return false;
  }
  if (integer_overflow) {
    failure = UserSettingsResult::OutOfRange;   // at least 10^17 means far above 1
    return false;
  }
  // value * 1e6 = digits * 10^(scale + 6), rounded half up.
  std::uint64_t result = 0;
  if (digits != 0) {
    const int shift = scale + 6;
    if (shift >= 0) {
      if (shift > 7) {
        failure = UserSettingsResult::OutOfRange;
        return false;
      }
      result = digits;
      for (int k = 0; k < shift; ++k) result *= 10;
    } else if (-shift <= 18) {
      std::uint64_t divisor = 1;
      for (int k = 0; k < -shift; ++k) divisor *= 10;
      result = digits / divisor;
      const std::uint64_t remainder = digits % divisor;
      if (remainder * 2 >= divisor) ++result;
    }
  }
  if (result > kMicroOne || (negative && result != 0)) {
    failure = UserSettingsResult::OutOfRange;
    return false;
  }
  micro = result;
  return true;
}

}  // namespace

UserSettingsResult parse_user_settings(const char* data, std::size_t length,
                                       UserSettings& out) noexcept {
  if (data == nullptr && length != 0) return UserSettingsResult::Malformed;
  if (length > kUserSettingsMaxBytes) return UserSettingsResult::TooLarge;
  UserSettings parsed = out;
  std::size_t position = 0;
  while (position < length) {
    std::size_t end = position;
    while (end < length && data[end] != '\n') ++end;
    std::size_t begin = position, stop = end;
    if (stop - begin > kUserSettingsLineChars) return UserSettingsResult::Malformed;
    while (begin < stop && is_space(data[begin])) ++begin;
    while (stop > begin && is_space(data[stop - 1])) --stop;
    position = end + 1;
    if (begin == stop) continue;
    const char* comma = static_cast<const char*>(std::memchr(data + begin, ',', stop - begin));
    if (comma == nullptr) return UserSettingsResult::Malformed;
    const std::size_t name_length = static_cast<std::size_t>(comma - (data + begin));
    if (name_length == kKeyNameLength &&
        std::memcmp(data + begin, kKeyName, kKeyNameLength) == 0) {
      std::uint64_t micro = 0;
      UserSettingsResult failure = UserSettingsResult::Ok;
      const std::size_t value_offset = static_cast<std::size_t>(comma - data) + 1;
      if (!parse_micro(comma + 1, stop - value_offset, micro, failure)) return failure;
      parsed.key_brightness = static_cast<float>(micro) / 1000000.0f;
    }
  }
  out = parsed;
  return UserSettingsResult::Ok;
}

UserSettingsResult encode_user_settings(const UserSettings& settings, char* output,
                                        std::size_t capacity, std::size_t& written) noexcept {
  written = 0;
  if (output == nullptr) return UserSettingsResult::OutputFull;
  float value = settings.key_brightness;
  if (!(value >= kUserKeyBrightnessMin)) value = kUserKeyBrightnessMin;   // also maps NaN to min
  if (value > kUserKeyBrightnessMax) value = kUserKeyBrightnessMax;
  const std::uint32_t micro = static_cast<std::uint32_t>(value * 1000000.0f + 0.5f);
  const std::uint32_t integer = micro / 1000000U;
  std::uint32_t fraction = micro % 1000000U;
  char digits[6] = {0};
  std::size_t count = 0;
  if (fraction != 0) {
    for (int i = 5; i >= 0; --i) { digits[i] = static_cast<char>('0' + fraction % 10); fraction /= 10; }
    count = 6;
    while (count > 0 && digits[count - 1] == '0') --count;
  }
  char line[kKeyNameLength + 1 + 10] = {0};
  std::size_t n = 0;
  std::memcpy(line, kKeyName, kKeyNameLength);
  n = kKeyNameLength;
  line[n++] = ',';
  line[n++] = static_cast<char>('0' + integer);   // value is 0 or 1 after clamping
  if (count != 0) {
    line[n++] = '.';
    for (std::size_t i = 0; i < count; ++i) line[n++] = digits[i];
  }
  line[n++] = '\n';
  if (n > capacity) return UserSettingsResult::OutputFull;
  std::memcpy(output, line, n);
  written = n;
  return UserSettingsResult::Ok;
}

}  // namespace orbit
