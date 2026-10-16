#pragma once

#include <cstddef>
#include <cstdint>

#include "domain/types.hpp"

namespace orbit {

constexpr std::size_t kSceneModuleSlots = 10;
constexpr std::size_t kSceneMaxModuleIdChars = 10;  // chord_walk
constexpr std::size_t kSceneMaxFieldNameChars = 21; // random_pattern_length
constexpr std::size_t kSceneMaxAttrNameChars = 11;  // step_values
constexpr std::size_t kSceneMaxParamsPerModule = 17; // matrix local fields plus Input controls
constexpr std::size_t kSceneMaxAttrsPerModule = 8;   // matrix
constexpr std::size_t kSceneMaxAttrValues = 12;      // matrix arrays
constexpr std::size_t kSceneMaxIntTextChars = 11;    // signed int32 decimal
constexpr std::size_t kSceneMaxFloatTextChars = 15;  // finite binary32, 9 significant digits
constexpr std::size_t kSceneMaxAttrValueTextChars =
    kSceneMaxAttrValues * kSceneMaxIntTextChars + (kSceneMaxAttrValues - 1);
constexpr std::size_t kSceneAttrLineChars =
    2 + kSceneMaxAttrNameChars + 1 + kSceneMaxAttrValueTextChars;
constexpr std::size_t kSceneParamLineChars =
    2 + kSceneMaxFieldNameChars + 1 + kSceneMaxFloatTextChars;
constexpr std::size_t kSceneMaxLineChars =
    kSceneAttrLineChars > kSceneParamLineChars ? kSceneAttrLineChars : kSceneParamLineChars;
constexpr std::size_t kSceneMaxLineBytes = kSceneMaxLineChars + 2; // CRLF
constexpr std::size_t kSceneBaseRecordLines = 159; // header, globals, modules, inherited fields, END
constexpr std::size_t kSceneHeadroomLines = 8;
constexpr std::size_t kSceneMaxBytes =
    (kSceneBaseRecordLines + kSceneHeadroomLines) * kSceneMaxLineBytes;

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

static_assert(kSceneMaxLineChars == 157, "scene line bound must match source inventory");
static_assert(kSceneMaxBytes == 26553, "scene byte bound must match documented derivation");

}  // namespace orbit
