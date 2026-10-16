#include "domain/music.hpp"
#include "platform/macropad/orion_assets.hpp"
#include <cassert>
#include <cstdint>
#include "fixtures/t03_golden.hpp"

constexpr std::uint32_t scale_checksum() {
  std::uint32_t hash = 2166136261U;
  for (std::size_t s = 0; s < orbit::music::kScales.size(); ++s)
    for (std::uint8_t i = 0; i < orbit::music::kScaleLengths[s]; ++i) {
      hash ^= orbit::music::kScales[s][i]; hash *= 16777619U;
    }
  return hash;
}
static_assert(orbit::music::kScales.size() == 21);
static_assert(scale_checksum() == 0xb679a576U, "source music.py scale checksum");

int main() {
  using namespace orbit::music;
  std::uint32_t golden_hash = 2166136261U;
  for (std::uint8_t scale = 0; scale < kScales.size(); ++scale)
    for (std::uint8_t root = 0; root < 12; ++root)
      for (const auto note : {0, 1, 11, 12, 59, 60, 61, 126, 127})
        for (const auto degree : {-24, -1, 0, 1, 24})
          for (const auto octave : {-2, 0, 2}) {
            golden_hash ^= transpose(note, degree, octave, true, root, scale);
            golden_hash *= 16777619U;
          }
  assert(golden_hash == music_golden::transpose_checksum);
  golden_hash = 2166136261U;
  const auto hash_byte = [&golden_hash](std::uint8_t byte) { golden_hash ^= byte; golden_hash *= 16777619U; };
  for (const auto value : kAutoBorrowRelationship) hash_byte(value);
  for (const auto value : kRateMidiTicks) hash_byte(value);
  for (const auto value : kPatternBits) hash_byte(value);
  for (const auto value : kPatternLengths) hash_byte(value);
  assert(golden_hash == music_golden::table_checksum);
  golden_hash = 2166136261U;
  const auto hash_text = [&hash_byte](const char* value) {
    do { hash_byte(static_cast<std::uint8_t>(*value)); } while (*value++);
  };
  for (const auto value : kNotes) hash_text(value);
  for (const auto value : kScaleNames) hash_text(value);
  for (const auto value : kRateLabels) hash_text(value);
  assert(golden_hash == music_golden::string_checksum);
  assert(kRateMidiTicks.size() == 13 && kPatternBits.size() == 25);
  assert(pattern_step(0b110, 3, 0) == 1 && pattern_step(0b110, 3, 1) == 1);
  assert(pattern_step(0b110, 3, 2) == 0 && pattern_step(0b110, 3, 3) == 1);
  assert(pattern_empty_steps(0b110, 3) == 1);
  assert(transpose(60, -1, 0, true, 0, 1) == 59);
  assert(transpose(60, 7, 0, true, 0, 1) == 72);
  assert(transpose(0, -1, 0, true, 0, 1) == 0);
  assert(transpose(127, 1, 0, true, 0, 1) == 127);
  assert(transpose(60, -1, 0, false, 0, 1) == 59);
  assert(transpose(60, INT32_MAX, INT32_MAX, true, 0, 1) == 127);
  assert(transpose(60, INT32_MIN, INT32_MIN, true, 0, 1) == 0);
  assert(transpose(60, INT32_MAX, INT32_MAX, false, 0, 1) == 127);
  assert(kDrumBaseNote == 36 && kDrumBottomRows[0] == 3 && kDrumBottomRowsFlipped[11] == 11);
  assert(kPadMap[0] == 9 && kChordRootPads4x8[7] == 11 && kDefaultChordModifiers[5] == 10);
  assert(orbit::assets::bmp_font_6x6_size == 400);
  assert(orbit::assets::bmp_launchscreen1_1_size == 1088);
}
