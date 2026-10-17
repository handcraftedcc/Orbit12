// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Derived from module.json manifests plus the fixed SCN1 rows (foundation-schema.md).
// Native core-only builds (ORBIT_NATIVE_CORE_ONLY) get the six-input baseline values.
#ifdef ORBIT_NATIVE_EXAMPLE_PROFILE
#include "../../../../../tests/native/generated/scene_layout_examples.hpp"
#else
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../domain/types.hpp"

namespace orbit {
struct SceneInputLayout {
  const char* id;
  std::uint8_t parameters;
  std::uint8_t attribute_values;
  std::uint8_t offset;
};
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneBaseRecordLines = 159;
#else
inline constexpr std::size_t kGeneratedSceneBaseRecordLines = 159;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneBankParameterLines = 58;
#else
inline constexpr std::size_t kGeneratedSceneBankParameterLines = 58;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneBankAttributeLines = 12;
#else
inline constexpr std::size_t kGeneratedSceneBankAttributeLines = 12;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneBankValues = 170;
#else
inline constexpr std::size_t kGeneratedSceneBankValues = 170;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneInputBanks = 6;
#else
inline constexpr std::size_t kGeneratedSceneInputBanks = 6;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneInputBankLines = 76;
#else
inline constexpr std::size_t kGeneratedSceneInputBankLines = 76;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxBytes = 38637;
#else
inline constexpr std::size_t kGeneratedSceneMaxBytes = 38637;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxLineChars = 157;
#else
inline constexpr std::size_t kGeneratedSceneMaxLineChars = 157;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxParamsPerModule = 17;
#else
inline constexpr std::size_t kGeneratedSceneMaxParamsPerModule = 17;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxAttrsPerModule = 8;
#else
inline constexpr std::size_t kGeneratedSceneMaxAttrsPerModule = 8;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxAttrValues = 12;
#else
inline constexpr std::size_t kGeneratedSceneMaxAttrValues = 12;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxFieldNameChars = 21;
#else
inline constexpr std::size_t kGeneratedSceneMaxFieldNameChars = 21;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxAttrNameChars = 11;
#else
inline constexpr std::size_t kGeneratedSceneMaxAttrNameChars = 11;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneMaxModuleIdChars = 10;
#else
inline constexpr std::size_t kGeneratedSceneMaxModuleIdChars = 10;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneProcessorParamMax = 15;
#else
inline constexpr std::size_t kGeneratedSceneProcessorParamMax = 15;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneProcessorAttrRowsMax = 3;
#else
inline constexpr std::size_t kGeneratedSceneProcessorAttrRowsMax = 3;
#endif
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::size_t kGeneratedSceneDtoBankValueBytes = 680;
#else
inline constexpr std::size_t kGeneratedSceneDtoBankValueBytes = 680;
#endif
inline constexpr SceneInputLayout kGeneratedSceneInputLayout[kGeneratedSceneInputBanks] = {
    {"note", 6, 0, 0},
    {"chords", 13, 0, 6},
    {"drum", 6, 0, 19},
    {"matrix", 17, 96, 25},
    {"note_walk", 5, 0, 138},
    {"chord_walk", 11, 16, 143},
};
// User-module attribute schema: { scene key, attribute name, type, element count }; terminated by a null key.
struct SceneAttributeRow {
  const char* scene_key;
  const char* name;
  AttributeType type;
  std::uint16_t element_count;
};
inline constexpr SceneAttributeRow kGeneratedSceneAttributeRows[] = {
    {nullptr, nullptr, AttributeType::Integer, 0},
};
}  // namespace orbit
#endif
