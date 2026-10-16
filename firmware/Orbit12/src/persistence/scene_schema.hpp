#pragma once

#include <cstddef>
#include <cstdint>

#include "../domain/types.hpp"
// Module-derived SCN1 bounds and the input layout (tools/modules/gen_registry.py). Defines SceneInputLayout.
#include "../modules/generated/scene_layout_generated.hpp"

namespace orbit {

// Format constants (SCN1 text widths and slot counts) stay hand-written. Everything derived from the
// module manifests is an alias of the generated value, so adding a module updates the layout without edits here.
constexpr std::size_t kSceneModuleSlots = 10;
constexpr std::size_t kSceneMaxModuleIdChars = kGeneratedSceneMaxModuleIdChars;
constexpr std::size_t kSceneMaxFieldNameChars = kGeneratedSceneMaxFieldNameChars;
constexpr std::size_t kSceneMaxAttrNameChars = kGeneratedSceneMaxAttrNameChars;
constexpr std::size_t kSceneMaxParamsPerModule = kGeneratedSceneMaxParamsPerModule;
constexpr std::size_t kSceneMaxAttrsPerModule = kGeneratedSceneMaxAttrsPerModule;
constexpr std::size_t kSceneMaxAttrValues = kGeneratedSceneMaxAttrValues;
constexpr std::size_t kSceneMaxIntTextChars = 11;    // signed int32 decimal
constexpr std::size_t kSceneMaxFloatTextChars = 15;  // finite binary32, 9 significant digits
constexpr std::size_t kSceneMaxAttrValueTextChars =
    kSceneMaxAttrValues * kSceneMaxIntTextChars + (kSceneMaxAttrValues - 1);
constexpr std::size_t kSceneAttrLineChars =
    2 + kSceneMaxAttrNameChars + 1 + kSceneMaxAttrValueTextChars;
constexpr std::size_t kSceneParamLineChars =
    2 + kSceneMaxFieldNameChars + 1 + kSceneMaxFloatTextChars;
constexpr std::size_t kSceneMaxLineChars = kGeneratedSceneMaxLineChars;
constexpr std::size_t kSceneMaxLineBytes = kSceneMaxLineChars + 2; // CRLF
constexpr std::size_t kSceneBaseRecordLines = kGeneratedSceneBaseRecordLines; // header, globals, modules, inherited fields, END

// Input edit banks (SCN1 "BANK" sections, keyed by module id): one section per input module (core and
// enabled user inputs, selector order). Parameter lines BP and attribute lines BA. Old firmware ignores
// the BANK/BP/BA tags as unknown lines.
constexpr std::size_t kSceneInputBanks = kGeneratedSceneInputBanks;
// Value region of each input in a bank: its persisted parameters, then its saved attribute elements.
inline constexpr const SceneInputLayout (&kSceneInputLayout)[kSceneInputBanks] = kGeneratedSceneInputLayout;
constexpr std::size_t kSceneBankValues = kGeneratedSceneBankValues;
constexpr std::size_t kSceneBankHeaderLines = kSceneInputBanks;
constexpr std::size_t kSceneBankParameterLines = kGeneratedSceneBankParameterLines;
constexpr std::size_t kSceneBankAttributeLines = kGeneratedSceneBankAttributeLines;
constexpr std::size_t kSceneInputBankLines = kGeneratedSceneInputBankLines;
constexpr std::size_t kSceneHeadroomLines = 8;
constexpr std::size_t kSceneMaxBytes = kGeneratedSceneMaxBytes;

constexpr bool scene_layout_consistent() noexcept {
  std::size_t total = 0;
  std::size_t parameters = 0;
  for (const auto& layout : kSceneInputLayout) {
    if (layout.offset != total) return false;
    total += layout.parameters + layout.attribute_values;
    parameters += layout.parameters;
  }
  return total == kSceneBankValues && parameters == kSceneBankParameterLines;
}
static_assert(scene_layout_consistent(), "bank regions must be contiguous and total kSceneBankValues");

enum class SceneModuleKind : std::uint8_t { Fixed, Input, Processor };
struct SceneAttributeSpec {
  const char* name = nullptr;
  AttributeType type = AttributeType::Integer;
  std::uint16_t element_count = 1;
};

// A resolver returns a stable, static schema for one ID in one slot. It can
// adapt ModuleDescriptor/InputDescriptor registries without constructing a
// live module; attributes have their own immutable schema span.
struct SceneModuleSchema {
  const char* id = nullptr;
  std::uint16_t version = 1;
  SceneModuleKind kind = SceneModuleKind::Processor;
  const ParamSpec* params = nullptr;
  std::uint8_t param_count = 0;
  const SceneAttributeSpec* attrs = nullptr;
  std::uint8_t attr_count = 0;
};

using SceneSchemaLookup = bool (*)(void*, std::uint8_t slot, const char* id,
                                   SceneModuleSchema& result);
struct SceneSchemaResolver {
  void* context = nullptr;
  SceneSchemaLookup lookup = nullptr;
  bool resolve(std::uint8_t slot, const char* id, SceneModuleSchema& result) const noexcept {
    return lookup != nullptr && lookup(context, slot, id, result);
  }
};

constexpr ParamSpec scene_integer_spec(const char* name, std::int32_t value,
                                       std::int32_t minimum,
                                       std::int32_t maximum) noexcept {
  return {name, ParamType::Integer, ParamValue::integer(value),
          ParamValue::integer(minimum), ParamValue::integer(maximum),
          ParamValue::integer(1), name, nullptr, 0, ParamPersist};
}
constexpr ParamSpec scene_enum_spec(const char* name, std::int32_t value,
                                    std::int32_t minimum,
                                    std::int32_t maximum) noexcept {
  return {name, ParamType::Enumeration, ParamValue::integer(value),
          ParamValue::integer(minimum), ParamValue::integer(maximum),
          ParamValue::integer(1), name, nullptr, 0, ParamPersist};
}
constexpr ParamSpec scene_decimal_spec(const char* name, float value,
                                       float minimum, float maximum) noexcept {
  return {name, ParamType::Decimal, ParamValue::decimal(value),
          ParamValue::decimal(minimum), ParamValue::decimal(maximum),
          ParamValue::decimal(0.01f), name, nullptr, 0, ParamPersist};
}

const SceneModuleSchema* fixed_scene_schema(std::uint8_t slot,
                                            const char* id) noexcept;

#ifdef ORBIT_NATIVE_CORE_ONLY
// Native core-only builds pin the six-input baseline (the SCN1 values before user modules existed).
constexpr bool same_text(const char* a, const char* b) noexcept {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
constexpr bool scene_core_baseline_layout() noexcept {
  constexpr SceneInputLayout kBaseline[6] = {
      {"note", 6, 0, 0}, {"chords", 13, 0, 6}, {"drum", 6, 0, 19},
      {"matrix", 17, 96, 25}, {"note_walk", 5, 0, 138}, {"chord_walk", 11, 16, 143}};
  for (std::size_t i = 0; i < 6; ++i) {
    if (!same_text(kSceneInputLayout[i].id, kBaseline[i].id) || kSceneInputLayout[i].parameters != kBaseline[i].parameters ||
        kSceneInputLayout[i].attribute_values != kBaseline[i].attribute_values || kSceneInputLayout[i].offset != kBaseline[i].offset) {
      return false;
    }
  }
  return true;
}
static_assert(kSceneInputBanks == 6, "six core inputs");
static_assert(scene_core_baseline_layout(), "kSceneInputLayout: six-input baseline");
static_assert(kSceneBaseRecordLines == 159 && kSceneBankValues == 170 && kSceneBankParameterLines == 58 &&
              kSceneBankAttributeLines == 12 && kSceneInputBankLines == 76, "six-input SCN1 derivation");
static_assert(kSceneMaxLineChars == 157, "scene line bound must match source inventory");
static_assert(kSceneMaxBytes == 38637, "scene byte bound: six-input baseline");
#endif

}  // namespace orbit
