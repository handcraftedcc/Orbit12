#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orbit::music {

inline constexpr std::uint8_t kDrumBaseNote = 36;
inline constexpr std::array<std::uint8_t, 12> kPadMap{{9,10,11,6,7,8,3,4,5,0,1,2}};
inline constexpr std::array<std::uint8_t, 12> kDrumBottomRows{{3,7,11,2,6,10,1,5,9,0,4,8}};
inline constexpr std::array<std::uint8_t, 12> kDrumBottomRowsFlipped{{0,4,8,1,5,9,2,6,10,3,7,11}};
inline constexpr std::array<std::uint8_t, 6> kChordRootPadsSplit{{0,1,2,3,4,5}};
inline constexpr std::array<std::uint8_t, 8> kChordRootPads4x8{{1,2,4,5,7,8,10,11}};
inline constexpr std::array<std::uint8_t, 6> kChordModifierPadsSplit{{6,7,8,9,10,11}};
inline constexpr std::array<std::uint8_t, 4> kChordModifierPads4x8{{0,3,6,9}};
inline constexpr std::array<std::uint8_t, 6> kDefaultChordModifiers{{4,7,8,5,1,10}};
inline constexpr std::array<const char*, 12> kNotes{{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
inline constexpr std::array<const char*, 21> kScaleNames{{"CHR", "MAJ", "MIN", "HMIN", "MMIN", "ION", "DOR", "PHR", "LYD", "MIX", "AEO", "LOC", "MAJP", "MINP", "SPEN", "BLU", "MIN6", "M7B", "WT", "DIMH", "DIMW"}};
inline constexpr std::array<std::array<std::uint8_t, 12>, 21> kScales{{
  {{0,1,2,3,4,5,6,7,8,9,10,11}}, {{0,2,4,5,7,9,11}}, {{0,2,3,5,7,8,10}}, {{0,2,3,5,7,8,11}}, {{0,2,3,5,7,9,11}},
  {{0,2,4,5,7,9,11}}, {{0,2,3,5,7,9,10}}, {{0,1,3,5,7,8,10}}, {{0,2,4,6,7,9,11}}, {{0,2,4,5,7,9,10}},
  {{0,2,3,5,7,8,10}}, {{0,1,3,5,6,8,10}}, {{0,2,4,7,9}}, {{0,3,5,7,10}}, {{0,2,5,7,10}}, {{0,3,5,6,7,10}},
  {{0,2,3,5,7,9,10}}, {{0,2,4,5,7,9,10}}, {{0,2,4,6,8,10}}, {{0,1,3,4,6,7,9,10}}, {{0,2,3,5,6,8,9,11}}
}};
inline constexpr std::array<std::uint8_t, 21> kScaleLengths{{12,7,7,7,7,7,7,7,7,7,7,7,5,5,5,6,7,7,6,8,8}};
inline constexpr std::array<std::uint8_t, 21> kAutoBorrowRelationship{{0,2,1,2,6,2,2,2,5,5,5,7,1,2,6,13,2,1,9,3,2}};
inline constexpr std::array<std::uint8_t, 13> kRateMidiTicks{{96,64,48,32,24,16,12,8,6,4,3,2,1}};
inline constexpr std::array<const char*, 13> kRateLabels{{"1/1","1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/32T","1/64"}};
inline constexpr std::array<std::uint8_t, 25> kPatternBits{{3,2,6,4,14,12,8,30,28,24,21,20,62,60,56,53,41,254,252,248,240,224,189,148,178}};
inline constexpr std::array<std::uint8_t, 25> kPatternLengths{{2,2,3,3,4,4,4,5,5,5,5,5,6,6,6,6,6,8,8,8,8,8,8,8,8}};

constexpr std::uint8_t pattern_step(std::uint8_t bits, std::uint8_t length, std::uint32_t step) noexcept {
  if (length == 0 || length > 8) return 0;
  const auto position = step % length;
  return static_cast<std::uint8_t>((bits >> (length - position - 1U)) & 1U);
}
constexpr std::uint8_t pattern_empty_steps(std::uint8_t bits, std::uint8_t length) noexcept {
  std::uint8_t count = 0;
  for (std::uint8_t i = 0; i < length && i < 8; ++i) if (((bits >> i) & 1U) == 0) ++count;
  return count;
}
constexpr std::uint8_t transpose(std::int32_t note, std::int32_t degrees, std::int32_t octaves,
                                 bool scale_aware, std::uint8_t root, std::uint8_t scale_id) noexcept {
  std::int64_t result = note;
  if (scale_id >= kScales.size()) scale_id = 1;
  if (scale_aware) {
    const auto relative = result - root;
    const auto normalized = ((relative % 12) + 12) % 12;
    const auto octave = relative >= 0 ? relative / 12 : (relative - 11) / 12;
    const auto length = kScaleLengths[scale_id];
    std::uint8_t index = 0;
    auto distance = static_cast<std::int32_t>(kScales[scale_id][0]) - normalized;
    if (distance < 0) distance = -distance;
    for (std::uint8_t i = 1; i < length; ++i) {
      auto candidate = static_cast<std::int32_t>(kScales[scale_id][i]) - normalized;
      if (candidate < 0) candidate = -candidate;
      if (candidate < distance) { distance = candidate; index = i; }
    }
    auto position = static_cast<std::int64_t>(index) + degrees;
    auto scale_octave = position / length;
    auto scale_index = position % length;
    if (scale_index < 0) { scale_index += length; --scale_octave; }
    result = root + (octave + scale_octave + octaves) * 12 + kScales[scale_id][static_cast<std::size_t>(scale_index)];
  } else result += static_cast<std::int64_t>(degrees) + static_cast<std::int64_t>(octaves) * 12;
  if (result < 0) return 0;
  if (result > 127) return 127;
  return static_cast<std::uint8_t>(result);
}
}  // namespace orbit::music
