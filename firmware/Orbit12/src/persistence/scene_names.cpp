#include "scene_names.hpp"

#include <cstring>

namespace orbit {
namespace {

constexpr const char* kNatoWords[] = {
    "ALFA",    "BRAVO",   "CHARLIE", "DELTA",   "ECHO",    "FOXTROT", "GOLF",   "HOTEL",
    "INDIA",   "JULIET",  "KILO",    "LIMA",    "MIKE",    "NOVEMBER", "OSCAR", "PAPA",
    "QUEBEC",  "ROMEO",   "SIERRA",  "TANGO",   "UNIFORM", "VICTOR",  "WHISKEY", "XRAY",
    "YANKEE",  "ZULU"};
constexpr std::size_t kNatoCount = sizeof(kNatoWords) / sizeof(kNatoWords[0]);
constexpr std::size_t kSuffixLimit = 99;

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool name_chars_valid(const char* text, std::size_t length) noexcept {
  if (length == 0 || length > kSceneNameMaxChars) return false;
  for (std::size_t i = 0; i < length; ++i) {
    const char c = text[i];
    const bool upper = c >= 'A' && c <= 'Z';
    if (!upper && !is_digit(c)) return false;
  }
  return true;
}

// Writes word (truncated to room chars) followed by the decimal suffix into out.
void compose_suffixed(const char* word, std::size_t suffix, char* out) noexcept {
  char digits[3]{};
  std::size_t digit_count = 0;
  if (suffix >= 10) { digits[digit_count++] = static_cast<char>('0' + suffix / 10); }
  digits[digit_count++] = static_cast<char>('0' + suffix % 10);
  std::size_t word_length = std::strlen(word);
  const std::size_t room = kSceneNameMaxChars - digit_count;
  if (word_length > room) word_length = room;
  std::memcpy(out, word, word_length);
  std::memcpy(out + word_length, digits, digit_count);
  out[word_length + digit_count] = '\0';
}

}  // namespace

std::size_t SceneNames::occupied_count() const noexcept {
  std::size_t count = 0;
  for (std::size_t slot = 0; slot < kSceneSlotCount; ++slot)
    if (occupied(slot)) ++count;
  return count;
}

void SceneNames::clear() noexcept {
  for (auto& entry : names) entry[0] = '\0';
  occupied_mask = 0;
  malformed_lines = 0;
}

void SceneNames::set(std::size_t slot, const char* text) noexcept {
  if (slot >= kSceneSlotCount) return;
  std::size_t length = 0;
  while (length < kSceneNameMaxChars && text[length] != '\0') {
    names[slot][length] = text[length];
    ++length;
  }
  names[slot][length] = '\0';
  occupied_mask = static_cast<std::uint16_t>(occupied_mask | (1U << slot));
}

bool name_valid(const char* text) noexcept {
  if (text == nullptr) return false;
  std::size_t length = 0;
  while (length <= kSceneNameMaxChars && text[length] != '\0') ++length;
  return name_chars_valid(text, length);
}

bool name_valid(const char* text, std::size_t length) noexcept {
  return text != nullptr && name_chars_valid(text, length);
}

bool name_taken(const SceneNames& names, const char* text) noexcept {
  if (text == nullptr) return false;
  for (std::size_t slot = 0; slot < kSceneSlotCount; ++slot)
    if (names.occupied(slot) && std::strcmp(names.names[slot], text) == 0) return true;
  return false;
}

bool first_free_slot(const SceneNames& names, std::uint8_t& slot) noexcept {
  for (std::size_t i = 0; i < kSceneSlotCount; ++i) {
    if (!names.occupied(i)) {
      slot = static_cast<std::uint8_t>(i);
      return true;
    }
  }
  return false;
}

bool first_occupied_slot(const SceneNames& names, std::uint8_t& slot) noexcept {
  for (std::size_t i = 0; i < kSceneSlotCount; ++i) {
    if (names.occupied(i)) {
      slot = static_cast<std::uint8_t>(i);
      return true;
    }
  }
  return false;
}

bool suggest_name(const SceneNames& names, char* out, std::size_t capacity) noexcept {
  if (out == nullptr || capacity < kSceneNameBytes) return false;
  for (std::size_t i = 0; i < kNatoCount; ++i) {
    if (!name_taken(names, kNatoWords[i])) {
      std::memcpy(out, kNatoWords[i], std::strlen(kNatoWords[i]) + 1);
      return true;
    }
  }
  // Exhausted: bounded cycle "<word><n>" truncated to 8 characters.
  for (std::size_t suffix = 2; suffix <= kSuffixLimit; ++suffix) {
    for (std::size_t i = 0; i < kNatoCount; ++i) {
      char candidate[kSceneNameBytes]{};
      compose_suffixed(kNatoWords[i], suffix, candidate);
      if (!name_taken(names, candidate)) {
        std::memcpy(out, candidate, kSceneNameBytes);
        return true;
      }
    }
  }
  return false;
}

SceneNamesResult parse_scene_names(const char* data, std::size_t length,
                                   SceneNames& out) noexcept {
  if (data == nullptr && length != 0) return SceneNamesResult::TooLarge;
  if (length > kSceneNamesMaxBytes) return SceneNamesResult::TooLarge;
  SceneNames parsed{};
  std::size_t position = 0;
  while (position < length) {
    std::size_t end = position;
    while (end < length && data[end] != '\n') ++end;
    std::size_t stop = end;
    if (stop > position && data[stop - 1] == '\r') --stop;
    const std::size_t line_length = stop - position;
    if (line_length == 0) {
      position = end + 1;
      continue;  // blank lines are not entries and not diagnostics
    }
    const char* line = data + position;
    position = end + 1;
    bool ok = line_length <= kSceneNamesLineChars && line_length >= 3 + 1 &&
              is_digit(line[0]) && is_digit(line[1]) && line[2] == ',';
    std::uint8_t slot = 0;
    if (ok) {
      const int number = (line[0] - '0') * 10 + (line[1] - '0');
      ok = number >= 1 && number <= static_cast<int>(kSceneSlotCount);
      slot = static_cast<std::uint8_t>(number - 1);
    }
    const char* name = line + 3;
    const std::size_t name_length = line_length - 3;
    ok = ok && name_chars_valid(name, name_length) && !parsed.occupied(slot);
    if (ok) {
      for (std::size_t i = 0; i < kSceneSlotCount; ++i)
        if (parsed.occupied(i) && name_length == std::strlen(parsed.names[i]) &&
            std::memcmp(parsed.names[i], name, name_length) == 0) {
          ok = false;
          break;
        }
    }
    if (!ok) {
      ++parsed.malformed_lines;
      continue;
    }
    char text[kSceneNameBytes]{};
    std::memcpy(text, name, name_length);
    parsed.set(slot, text);
  }
  out = parsed;
  return SceneNamesResult::Ok;
}

SceneNamesResult encode_scene_names(const SceneNames& names, char* output,
                                    std::size_t capacity, std::size_t& written) noexcept {
  written = 0;
  if (output == nullptr) return SceneNamesResult::OutputFull;
  std::size_t used = 0;
  for (std::size_t slot = 0; slot < kSceneSlotCount; ++slot) {
    if (!names.occupied(slot)) continue;
    const std::size_t number = slot + 1;
    const std::size_t name_length = std::strlen(names.names[slot]);
    const std::size_t line = 2 + 1 + name_length + 1;
    if (used + line > capacity) return SceneNamesResult::OutputFull;
    output[used++] = static_cast<char>('0' + number / 10);
    output[used++] = static_cast<char>('0' + number % 10);
    output[used++] = ',';
    std::memcpy(output + used, names.names[slot], name_length);
    used += name_length;
    output[used++] = '\n';
  }
  written = used;
  return SceneNamesResult::Ok;
}

bool parse_active_pointer(const char* text, std::size_t count, std::uint8_t& slot) noexcept {
  if (text == nullptr || count < 2 || text[count - 1] != '\n') return false;
  std::size_t digits = count - 1;
  if (digits != 0 && text[digits - 1] == '\r') --digits;
  if (digits == 0 || digits > 2 || text[0] < '1' || text[0] > '9') return false;
  int number = text[0] - '0';
  if (digits == 2) {
    if (!is_digit(text[1])) return false;
    number = number * 10 + (text[1] - '0');
  }
  if (number < 1 || number > static_cast<int>(kSceneSlotCount)) return false;
  slot = static_cast<std::uint8_t>(number - 1);
  return true;
}

std::size_t format_active_pointer(std::uint8_t slot, char* output, std::size_t capacity) noexcept {
  if (output == nullptr || slot >= kSceneSlotCount) return 0;
  const std::size_t number = slot + 1U;
  std::size_t length = 0;
  if (number >= 10) {
    if (capacity < 3) return 0;
    output[length++] = static_cast<char>('0' + number / 10);
  } else if (capacity < 2) {
    return 0;
  }
  output[length++] = static_cast<char>('0' + number % 10);
  output[length++] = '\n';
  return length;
}

}  // namespace orbit
