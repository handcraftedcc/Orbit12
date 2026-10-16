#include "../../common/module_state.hpp"
#include "../../../domain/music.hpp"
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

namespace orbit {
namespace {
constexpr std::uint8_t kNone = 0;
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr const char* kEmptyLabel = "EMPTY";
constexpr const char* kTransposeSemiLabel = "SEMI";
constexpr const char* kTransposeOctaveLabel = "OCT";
constexpr const char* kScaleAwareLabel = "INSCL";
constexpr const char* kOffsetLabels[] = {"OFS1", "OFS2", "OFS3", "OFS4", "OFS5"};
const ParamSpec kEmptyParams[] = {{"pick", ParamType::Action, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), kEmptyLabel, nullptr, 0, ParamUiOnly}};
const ParamSpec kTransposeParams[] = {
    {"semitones", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kTransposeSemiLabel, nullptr, 0, ParamPersist},
    {"octaves", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kTransposeOctaveLabel, nullptr, 0, ParamPersist},
    {"scale_aware", ParamType::Boolean, ParamValue::boolean(true), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), kScaleAwareLabel, nullptr, 0, ParamPersist}};
const ParamSpec kChordParams[] = {
    {"offset1", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOffsetLabels[0], nullptr, 0, ParamPersist},
    {"offset2", ParamType::Integer, ParamValue::integer(2), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOffsetLabels[1], nullptr, 0, ParamPersist},
    {"offset3", ParamType::Integer, ParamValue::integer(4), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOffsetLabels[2], nullptr, 0, ParamPersist},
    {"offset4", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOffsetLabels[3], nullptr, 0, ParamPersist},
    {"offset5", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(1), kOffsetLabels[4], nullptr, 0, ParamPersist},
    {"scale_aware", ParamType::Boolean, ParamValue::boolean(true), ParamValue::boolean(false), ParamValue::boolean(true), ParamValue::boolean(false), kScaleAwareLabel, nullptr, 0, ParamPersist}};

std::uint8_t safe_transpose(std::uint8_t note, std::int32_t degrees, std::int32_t octaves,
                            bool scale_aware, std::uint8_t root, std::uint8_t scale_id) noexcept {
  return music::transpose(note, degrees, octaves, scale_aware, root, scale_id);
}
void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kEmptyDescriptor{"empty", 1, kEmptyParams, 1, construct_empty_state, descriptor_destroy, process_empty, reset_empty_state, nullptr, attr_none};
const ModuleDescriptor kTransposeDescriptor{"transpose", 1, kTransposeParams, 3, construct_transpose_state, descriptor_destroy, process_transpose, reset_transpose_state, set_transpose_param, attr_none};
const ModuleDescriptor kChordDescriptor{"chord", 1, kChordParams, 6, construct_chord_state, descriptor_destroy, process_chord, reset_chord_state, set_chord_param, attr_none};
}  // namespace

bool NoteRelationships::add(std::uint8_t in, std::uint8_t out) noexcept {
  if (count >= kCapacity) return false;
  input[count] = in; output[count] = out; ++count; return true;
}
bool NoteRelationships::remove_first(std::uint8_t in, std::uint8_t& out) noexcept {
  for (std::uint8_t i = 0; i < count; ++i) if (input[i] == in) {
    out = output[i];
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < count; ++j) { input[j - 1] = input[j]; output[j - 1] = output[j]; }
    --count; return true;
  }
  return false;
}
std::uint8_t NoteRelationships::remove_all(std::uint8_t in, std::uint8_t* removed, std::uint8_t capacity) noexcept {
  std::uint8_t returned = 0, i = 0;
  while (i < count) {
    if (input[i] == in) {
      if (returned < capacity) removed[returned++] = output[i];
      for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < count; ++j) { input[j - 1] = input[j]; output[j - 1] = output[j]; }
      --count;
    } else ++i;
  }
  return returned;
}
bool NoteRelationships::has_output(std::uint8_t out) const noexcept {
  for (std::uint8_t i = 0; i < count; ++i) if (output[i] == out) return true;
  return false;
}
static_assert(std::is_trivially_destructible<ModuleState>::value,
              "in-place states need explicit destruction if nontrivial members are introduced");
void construct_empty_state(ModuleState& state) noexcept { new (&state.storage.empty) EmptyModuleState{}; }
void construct_transpose_state(ModuleState& state) noexcept { new (&state.storage.transpose) TransposeModuleState{}; }
void construct_chord_state(ModuleState& state) noexcept { new (&state.storage.chord) ChordModuleState{}; }
void process_empty(ModuleState&, const ProcessContext&, const NoteBatch& input, NoteBatch& output) noexcept { output = input; }
void process_transpose(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.transpose; output = {};
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const auto& note = input.on.items[i];
    if (s.relationships.count >= NoteRelationships::kCapacity || output.on.count >= kPolyphony) { ++s.relationship_overflow; continue; }
    const auto pitch = safe_transpose(note.pitch, s.semitones, s.octaves, s.scale_aware, context.global.key, context.global.scale);
    if (output.on.append({pitch, note.velocity, note.channel}) == PushResult::Added)
      (void)s.relationships.add(note.pitch, pitch);
  }
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const auto& note = input.off.items[i]; std::uint8_t pitch = 0;
    if (s.relationships.remove_first(note.pitch, pitch)) (void)output.off.append({pitch, 0, note.channel});
  }
}
void process_chord(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.chord; output = {};
  std::uint8_t removed[NoteRelationships::kCapacity]{};
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const auto& note = input.off.items[i];
    const auto removed_count = s.relationships.remove_all(note.pitch, removed, NoteRelationships::kCapacity);
    for (std::uint8_t j = 0; j < removed_count; ++j)
      if (!s.relationships.has_output(removed[j])) (void)output.off.append({removed[j], 0, note.channel});
  }
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const auto& note = input.on.items[i];
    for (std::uint8_t offset_index = 0; offset_index < 5; ++offset_index) {
      bool duplicate = false;
      for (std::uint8_t earlier = 0; earlier < offset_index; ++earlier)
        if (s.offsets[earlier] == s.offsets[offset_index]) duplicate = true;
      if (duplicate) continue;
      if (s.relationships.count >= NoteRelationships::kCapacity) { ++s.relationship_overflow; continue; }
      const auto pitch = safe_transpose(note.pitch, s.offsets[offset_index], 0, s.scale_aware, context.global.key, context.global.scale);
      if (s.relationships.has_output(pitch)) {
        if (output.on.count >= kPolyphony || output.off.count >= kPolyphony) { ++s.output_overflow; continue; }
        if (output.off.append({pitch, 0, note.channel}) != PushResult::Added) { ++s.output_overflow; continue; }
      }
      if (output.on.append({pitch, note.velocity, note.channel}) != PushResult::Added) { ++s.output_overflow; continue; }
      (void)s.relationships.add(note.pitch, pitch);
    }
  }
}
void reset_empty_state(ModuleState&, ResetReason) noexcept {}
void reset_transpose_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic || reason == ResetReason::Init) {
    state.storage.transpose.relationships.clear();
  }
}
void reset_chord_state(ModuleState& state, ResetReason reason) noexcept {
  if (reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic || reason == ResetReason::Init)
    state.storage.chord.relationships.clear();
}
bool set_transpose_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.transpose;
  if (index == 0 && value.type == ParamType::Integer) s.semitones = value.value.integer;
  else if (index == 1 && value.type == ParamType::Integer) s.octaves = value.value.integer;
  else if (index == 2 && value.type == ParamType::Boolean) s.scale_aware = value.value.boolean;
  else return false;
  return true;
}
bool set_chord_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.chord;
  if (index < 5 && value.type == ParamType::Integer) s.offsets[index] = value.value.integer;
  else if (index == 5 && value.type == ParamType::Boolean) s.scale_aware = value.value.boolean;
  else return false;
  return true;
}
void empty_attributes(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor& empty_module_descriptor() noexcept { return kEmptyDescriptor; }
const ModuleDescriptor& transpose_module_descriptor() noexcept { return kTransposeDescriptor; }
const ModuleDescriptor& chord_module_descriptor() noexcept { return kChordDescriptor; }
}  // namespace orbit
