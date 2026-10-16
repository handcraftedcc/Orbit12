#include <cmath>
#include <cstdio>
#include <cstring>

#include "persistence/scene_codec.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

constexpr orbit::ParamSpec kNoteParams[] = {
    orbit::scene_integer_spec("velocity", 100, 0, 127)};
constexpr orbit::ParamSpec kProbeParams[] = {
    orbit::scene_integer_spec("step_length", 2, 1, 8),
    orbit::scene_decimal_spec("gate", 100.0f, 0.0f, 100000.0f),
    {"randomize_steps", orbit::ParamType::Action, orbit::ParamValue::integer(0),
     orbit::ParamValue::integer(0), orbit::ParamValue::integer(0),
     orbit::ParamValue::integer(1), "RAND", nullptr, 0, orbit::ParamUiOnly},
};
constexpr orbit::SceneAttributeSpec kProbeAttrs[] = {
    {"step_values", orbit::AttributeType::Integer, 8}};

bool lookup(void*, std::uint8_t slot, const char* id, orbit::SceneModuleSchema& out) {
  if (slot == 1 && std::strcmp(id, "note") == 0) {
    out = {"note", 1, orbit::SceneModuleKind::Input, kNoteParams, 1, nullptr, 0};
    return true;
  }
  if (slot >= 2 && slot <= 7 && std::strcmp(id, "arp_walk") == 0) {
    out = {"arp_walk", 1, orbit::SceneModuleKind::Processor, kProbeParams, 3, kProbeAttrs, 1};
    return true;
  }
  if (slot >= 2 && slot <= 7 && std::strcmp(id, "empty") == 0) {
    out = {"empty", 1, orbit::SceneModuleKind::Processor, nullptr, 0, nullptr, 0};
    return true;
  }
  return false;
}

struct TestReader { const char* data; std::size_t size; std::size_t pos; bool error; };
orbit::SceneReadStatus test_read(void* context, char& byte) {
  auto& reader = *static_cast<TestReader*>(context);
  if (reader.error) return orbit::SceneReadStatus::Error;
  if (reader.pos == reader.size) return orbit::SceneReadStatus::End;
  byte = reader.data[reader.pos++];
  return orbit::SceneReadStatus::Byte;
}
orbit::SceneReadStatus missing_read(void*, char&) { return orbit::SceneReadStatus::Missing; }
struct TestWriter { char* data; std::size_t size; std::size_t capacity; bool error; };
orbit::SceneWriteStatus test_write(void* context, const char* data, std::size_t size) {
  auto& writer = *static_cast<TestWriter*>(context);
  if (writer.error) return orbit::SceneWriteStatus::Error;
  if (size > writer.capacity - writer.size) return orbit::SceneWriteStatus::Full;
  std::memcpy(writer.data + writer.size, data, size);
  writer.size += size;
  return orbit::SceneWriteStatus::Written;
}

const orbit::SceneSchemaResolver schema{nullptr, lookup};

void source_scene_round_trip() {
  // SCN1 order/record fields derive from scenes.py write_state/write_module.
  static constexpr char fixture[] =
      "SCN1\n"
      "S,key,2\nS,scale,3\nS,octave,-2147483648\nS,key_offset,2147483647\n"
      "S,bpm,120\nS,swing,0.375\nS,transport_mode,1\n"
      "M,2,arp_walk,1\nP,step_length,4\nP,gate,0.125\nP,legacy_unknown,999\n"
      "A,step_values,-1|2|0|0|0|0|0|0\nEND\n";
  orbit::SceneDto decoded{};
  CHECK(orbit::decode_scene(fixture, sizeof(fixture) - 1, schema, decoded) == orbit::SceneResult::Ok);
  CHECK(decoded.global_fields == 0x7fU);
  CHECK(decoded.globals.octave == INT32_MIN && decoded.globals.key_offset == INT32_MAX);
  const auto& module = decoded.modules[2];
  CHECK(module.present && std::strcmp(module.id, "arp_walk") == 0);
  CHECK(module.parameter_count == 2 && module.attribute_count == 1);
  CHECK(module.parameters[0].value.integer == 4);
  CHECK(std::fabs(module.parameters[1].value.decimal - 0.125f) < 0.000001f);
  CHECK(module.attributes[0].count == 8 && module.attributes[0].values[1].integer == 2);

  char encoded[orbit::kSceneMaxBytes]{};
  std::size_t written = 0;
  CHECK(orbit::encode_scene(decoded, schema, encoded, sizeof(encoded), written) == orbit::SceneResult::Ok);
  orbit::SceneDto round_trip{};
  CHECK(orbit::decode_scene(encoded, written, schema, round_trip) == orbit::SceneResult::Ok);
  CHECK(round_trip.globals.octave == decoded.globals.octave);
  CHECK(round_trip.modules[2].attributes[0].values[1].integer == 2);
  CHECK(std::fabs(round_trip.modules[2].parameters[1].value.decimal - 0.125f) < 0.000001f);

  TestWriter stream_out{encoded, 0, sizeof(encoded), false};
  CHECK(orbit::encode_scene(decoded, schema, {&stream_out, test_write}, written) == orbit::SceneResult::Ok);
  CHECK(stream_out.size == written);
  TestReader stream_in{encoded, stream_out.size, 0, false};
  orbit::SceneDto streamed{};
  CHECK(orbit::decode_scene({&stream_in, test_read}, schema, streamed) == orbit::SceneResult::Ok);
  CHECK(streamed.valid && streamed.modules[2].parameter_count == 2);
  static constexpr char crlf[] = "SCN1\r\nEND\r\n";
  CHECK(orbit::decode_scene(crlf, sizeof(crlf) - 1, schema, streamed) == orbit::SceneResult::Ok);
  CHECK(streamed.valid);
}

void legacy_and_fallback() {
  static constexpr char legacy[] =
      "SCN1\nM,0,note,1\nP,velocity,81\n"
      "M,1,transport,1\nP,bpm,300\n"
      "M,3,missing_module,99\nP,anything,3\nEND\n";
  orbit::SceneDto scene{};
  CHECK(orbit::decode_scene(legacy, sizeof(legacy) - 1, schema, scene) == orbit::SceneResult::Ok);
  CHECK(scene.modules[1].present && std::strcmp(scene.modules[1].id, "note") == 0);
  CHECK(scene.modules[0].present && std::strcmp(scene.modules[0].id, "set") == 0);
  CHECK(scene.modules[0].parameter_count == 1 && scene.modules[0].parameters[0].value.integer == 300);
  CHECK(scene.modules[3].unknown_module && std::strcmp(scene.modules[3].id, "empty") == 0);
  CHECK((scene.diagnostics & orbit::SceneUnknownModule) != 0);
}

void rejection_is_transactional() {
  orbit::SceneDto staging{};
  static constexpr char future[] = "SCN2\nEND\n";
  CHECK(orbit::decode_scene(future, sizeof(future) - 1, schema, staging) == orbit::SceneResult::Unsupported);
  CHECK(!staging.valid);
  static constexpr char future_module[] = "SCN1\nM,2,arp_walk,2\nEND\n";
  CHECK(orbit::decode_scene(future_module, sizeof(future_module) - 1, schema, staging) == orbit::SceneResult::Unsupported);
  CHECK(!staging.valid);
  static constexpr char truncated[] = "SCN1\nS,bpm,120\nM,2,arp_walk,1\nP,gate,0.4";
  CHECK(orbit::decode_scene(truncated, sizeof(truncated) - 1, schema, staging) == orbit::SceneResult::Malformed);
  CHECK(!staging.valid);
  static constexpr char invalid[] = "SCN1\nS,bpm,301\nEND\n";
  CHECK(orbit::decode_scene(invalid, sizeof(invalid) - 1, schema, staging) == orbit::SceneResult::InvalidValue);
  CHECK(!staging.valid);
  CHECK(orbit::decode_scene("SCN1", orbit::kSceneMaxBytes + 1U, schema, staging) == orbit::SceneResult::TooLarge);

  static constexpr char bad_array[] = "SCN1\nM,2,arp_walk,1\nA,step_values,1|2|3|4|5|6|7|8|9\nEND\n";
  CHECK(orbit::decode_scene(bad_array, sizeof(bad_array) - 1, schema, staging) == orbit::SceneResult::CapacityExceeded);
  CHECK(!staging.valid);
  static constexpr char short_array[] = "SCN1\nM,2,arp_walk,1\nA,step_values,1|2|3|4|5|6|7\nEND\n";
  CHECK(orbit::decode_scene(short_array, sizeof(short_array) - 1, schema, staging) == orbit::SceneResult::CapacityExceeded);
  CHECK(!staging.valid);
  CHECK(orbit::decode_scene({nullptr, missing_read}, schema, staging) == orbit::SceneResult::Missing);
  TestReader failed{nullptr, 0, 0, true};
  CHECK(orbit::decode_scene({&failed, test_read}, schema, staging) == orbit::SceneResult::IoError);
  static constexpr char empty_scene[] = "SCN1\nEND\n";
  CHECK(orbit::decode_scene(empty_scene, sizeof(empty_scene) - 1, schema, staging) == orbit::SceneResult::Ok);
  TestWriter failed_writer{nullptr, 0, 0, true};
  std::size_t written = 0;
  CHECK(orbit::encode_scene(staging, schema, {&failed_writer, test_write}, written) == orbit::SceneResult::IoError);
}

void float_and_line_bounds() {
  static constexpr char float_fixture[] =
      "SCN1\nM,2,arp_walk,1\nP,gate,0.12345679\nEND\n";
  orbit::SceneDto scene{};
  CHECK(orbit::decode_scene(float_fixture, sizeof(float_fixture) - 1, schema, scene) == orbit::SceneResult::Ok);
  char output[orbit::kSceneMaxBytes]{};
  std::size_t written = 0;
  CHECK(orbit::encode_scene(scene, schema, output, sizeof(output), written) == orbit::SceneResult::Ok);
  orbit::SceneDto copy{};
  CHECK(orbit::decode_scene(output, written, schema, copy) == orbit::SceneResult::Ok);
  CHECK(std::fabs(copy.modules[2].parameters[0].value.decimal - scene.modules[2].parameters[0].value.decimal) < 0.000001f);

  char line[5 + orbit::kSceneMaxLineChars + 1]{};
  std::memcpy(line, "SCN1\n", 5);
  std::memset(line + 5, 'x', orbit::kSceneMaxLineChars + 1);
  CHECK(orbit::decode_scene(line, 5 + orbit::kSceneMaxLineChars + 1, schema, copy) == orbit::SceneResult::TooLarge);
}

void source_numeric_and_unknown_fields() {
  // scenes.py int(float(text)) truncates toward zero; unknown S fields are
  // ignored before parsing, and Boolean text accepts the Python spellings.
  static constexpr char fixture[] =
      "SCN1\nS,future_field,opaque\nS,octave,-3.9\n"
      "M,2,arp_walk,1\nP,step_length,4e0\n"
      "A,step_values,-1.9|2e0|0|0|0|0|0|0\n"
      "M,8,output,1\nP,print,True\nEND\n";
  orbit::SceneDto scene{};
  CHECK(orbit::decode_scene(fixture, sizeof(fixture)-1, schema, scene) == orbit::SceneResult::Ok);
  CHECK(scene.valid && scene.globals.octave == -3);
  CHECK(scene.modules[2].parameters[0].value.integer == 4);
  CHECK(scene.modules[2].attributes[0].values[0].integer == -1);
  CHECK(scene.modules[8].parameters[0].value.boolean);
}

}  // namespace

int main() {
  source_scene_round_trip();
  legacy_and_fallback();
  rejection_is_transactional();
  float_and_line_bounds();
  source_numeric_and_unknown_fields();
  if (failures) return 1;
  std::puts("T07a scene codec checks passed");
  return 0;
}
