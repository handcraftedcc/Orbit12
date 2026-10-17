// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Derived from module.json manifests plus the fixed SCN1 rows (foundation-schema.md).
// Native core-only builds (ORBIT_NATIVE_CORE_ONLY) get the six-input baseline values.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../../firmware/Orbit12/src/domain/types.hpp"

namespace orbit {
struct SceneInputLayout {
  const char* id;
  std::uint8_t parameters;
  std::uint8_t attribute_values;
  std::uint8_t offset;
};
inline constexpr std::size_t kGeneratedSceneBaseRecordLines = 159;
inline constexpr std::size_t kGeneratedSceneBankParameterLines = 62;
inline constexpr std::size_t kGeneratedSceneBankAttributeLines = 13;
inline constexpr std::size_t kGeneratedSceneBankValues = 186;
inline constexpr std::size_t kGeneratedSceneInputBanks = 7;
inline constexpr std::size_t kGeneratedSceneInputBankLines = 82;
inline constexpr std::size_t kGeneratedSceneMaxBytes = 39591;
inline constexpr std::size_t kGeneratedSceneMaxLineChars = 157;
inline constexpr std::size_t kGeneratedSceneMaxParamsPerModule = 17;
inline constexpr std::size_t kGeneratedSceneMaxAttrsPerModule = 8;
inline constexpr std::size_t kGeneratedSceneMaxAttrValues = 12;
inline constexpr std::size_t kGeneratedSceneMaxFieldNameChars = 21;
inline constexpr std::size_t kGeneratedSceneMaxAttrNameChars = 11;
inline constexpr std::size_t kGeneratedSceneMaxModuleIdChars = 10;
inline constexpr std::size_t kGeneratedSceneProcessorParamMax = 15;
inline constexpr std::size_t kGeneratedSceneProcessorAttrRowsMax = 3;
inline constexpr std::size_t kGeneratedSceneDtoBankValueBytes = 744;
inline constexpr SceneInputLayout kGeneratedSceneInputLayout[kGeneratedSceneInputBanks] = {
    {"note", 6, 0, 0},
    {"chords", 13, 0, 6},
    {"drum", 6, 0, 19},
    {"matrix", 17, 96, 25},
    {"note_walk", 5, 0, 138},
    {"chord_walk", 11, 16, 143},
    {"ex_padvel", 4, 12, 170},
};
// User-module attribute schema: { scene key, attribute name, type, element count }; terminated by a null key.
struct SceneAttributeRow {
  const char* scene_key;
  const char* name;
  AttributeType type;
  std::uint16_t element_count;
};
inline constexpr SceneAttributeRow kGeneratedSceneAttributeRows[] = {
    {"ex_padvel", "pad_vel", AttributeType::Byte, 12},
    {nullptr, nullptr, AttributeType::Integer, 0},
};
}  // namespace orbit
