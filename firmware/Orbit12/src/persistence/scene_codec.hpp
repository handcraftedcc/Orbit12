#pragma once

#include <cstddef>
#include <cstdint>

#include "persistence/scene_schema.hpp"

namespace orbit {

enum class SceneResult : std::uint8_t {
  Ok, Missing, IoError, TooLarge, Malformed, Unsupported, InvalidValue, CapacityExceeded, InvalidSchema
};
enum class SceneReadStatus : std::uint8_t { Byte, End, Missing, Error };
enum class SceneWriteStatus : std::uint8_t { Written, Full, Error };
struct SceneReader {
  void* context = nullptr;
  SceneReadStatus (*read)(void*, char&) = nullptr;
};
struct SceneWriter {
  void* context = nullptr;
  SceneWriteStatus (*write)(void*, const char*, std::size_t) = nullptr;
};
enum class SceneValueKind : std::uint8_t { Integer, Decimal, Boolean };
union SceneScalar {
  std::int32_t integer;
  float decimal;
  bool boolean;
  constexpr SceneScalar() : integer(0) {}
};
struct SceneParameter {
  char name[kSceneMaxFieldNameChars + 1]{};
  SceneValueKind kind = SceneValueKind::Integer;
  SceneScalar value{};
};
struct SceneAttribute {
  char name[kSceneMaxAttrNameChars + 1]{};
  AttributeType type = AttributeType::Integer;
  std::uint16_t count = 0;
  SceneScalar values[kSceneMaxAttrValues]{};
};
struct SceneModule {
  bool present = false;
  bool unknown_module = false;
  char id[kSceneMaxModuleIdChars + 1]{};
  std::uint16_t version = 1;
  std::uint8_t parameter_count = 0;
  SceneParameter parameters[kSceneMaxParamsPerModule]{};
  std::uint8_t attribute_count = 0;
  SceneAttribute attributes[kSceneMaxAttrsPerModule]{};
};
enum SceneGlobalField : std::uint8_t {
  SceneKey = 1U << 0, SceneScale = 1U << 1, SceneOctave = 1U << 2,
  SceneKeyOffset = 1U << 3, SceneBpm = 1U << 4, SceneSwing = 1U << 5,
  SceneTransportMode = 1U << 6
};
enum SceneDiagnostic : std::uint8_t { SceneDiagnosticNone = 0, SceneUnknownModule = 1U << 0 };
// Parked input banks (see kSceneInputLayout). Bit i of `present` set: bank i (layout order)
// is in the scene. Values of bank i live at layout[i].offset; `parameters` bit k is the k-th
// persisted parameter slot, `attributes` bit j the j-th attribute of the schema.
struct SceneInputBankMeta {
  std::uint32_t parameters = 0;
  std::uint8_t attributes = 0;
};
struct SceneInputBanks {
  std::uint8_t present = 0;
  SceneInputBankMeta meta[kSceneInputBanks]{};
  SceneScalar values[kSceneBankValues]{};
};
struct SceneDto {
  GlobalState globals{};
  std::uint8_t global_fields = 0;
  SceneModule modules[kSceneModuleSlots]{};
  SceneInputBanks banks{};
  std::uint8_t diagnostics = SceneDiagnosticNone;
  bool valid = false;
};

// Persisted parameters are the ones the codec writes (field_persisted); their kind in SCN1 and
// in SceneParameter is Integer (integer and enumeration), Decimal or Boolean.
bool scene_param_persisted(const ParamSpec& spec) noexcept;
SceneValueKind scene_param_value_kind(const ParamSpec& spec) noexcept;
// Layout index of an input bank id (kSceneInputBanks when the id has no bank).
std::size_t scene_bank_index(const char* id) noexcept;
// Scalar index of attribute j's first element in a bank region.
std::size_t scene_bank_attribute_base(const SceneInputLayout& layout, const SceneModuleSchema& schema,
                                      std::uint8_t j) noexcept;

SceneResult decode_scene(SceneReader reader, const SceneSchemaResolver& schema,
                         SceneDto& staging) noexcept;
SceneResult decode_scene(const char* data, std::size_t length, const SceneSchemaResolver& schema,
                         SceneDto& staging) noexcept;
SceneResult encode_scene(const SceneDto& scene, const SceneSchemaResolver& schema,
                         SceneWriter writer, std::size_t& written) noexcept;
SceneResult encode_scene(const SceneDto& scene, const SceneSchemaResolver& schema,
                         char* output, std::size_t capacity,
                         std::size_t& written) noexcept;

// Allocation-free SCN1 scalar text. Exposed for native verification; the codec
// is the production caller. Formatting reproduces printf("%.9g") of the exact
// binary32 value (ties to even, sign-prefixed -0, exponent with at least two
// digits). Parsing accepts the strtof decimal grammar (leading ASCII space, sign,
// digits, optional point and exponent) in at most 15 characters and rounds like
// (float)strtod, which is what the pinned newlib strtof does. Hex, inf, nan and
// overflow are rejected.
bool scene_format_float(float value, char* out, std::size_t capacity,
                        std::size_t& length) noexcept;
bool scene_parse_float(const char* text, std::size_t length, float& value) noexcept;
// Decimal text with no leading space, truncated toward zero after one binary64
// rounding, as trunc(strtod(text)). Results outside int32 are rejected.
bool scene_parse_integer_like(const char* text, std::size_t length,
                              std::int32_t& value) noexcept;

}  // namespace orbit
