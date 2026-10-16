// T13 interrupted transfers: a power cut after each mutating storage operation of a
// CDC PUT. After reboot, boot-time recovery through SceneService must leave the file
// either as the old or the new content, never corrupt, and an acknowledged PUT must
// have stored the new content.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "persistence/maintenance_protocol.hpp"
#include "persistence/scene_codec.hpp"
#include "persistence/scene_service.hpp"
#include "t13_fs.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

namespace mt = orbit::maintenance;
using orbit::SceneFileId;
using orbit::SceneFileKind;

bool schema_lookup(void*, std::uint8_t slot, const char* id, orbit::SceneModuleSchema& result) {
  const auto* schema = orbit::fixed_scene_schema(slot, id);
  if (schema == nullptr) return false;
  result = *schema;
  return true;
}

bool lc_true(void*) { return true; }
void lc_void(void*) {}
std::uint32_t lc_quiesce(void*, std::uint32_t, std::uint32_t) { return 1; }
bool lc_quiesced(void*, std::uint32_t) { return true; }
bool lc_timed_out(void*, std::uint32_t, std::uint32_t) { return false; }
void lc_cancel(void*, std::uint32_t) {}
void lc_resume(void*, std::uint32_t) {}
bool lc_capture(void*, orbit::SceneDto&) { return true; }
bool lc_globals(void*, const orbit::GlobalState&, std::uint8_t) { return true; }
bool lc_construct(void*, std::uint8_t, const char*, std::uint16_t, bool) { return true; }
bool lc_param(void*, std::uint8_t, const orbit::SceneParameter&) { return true; }
bool lc_attr(void*, std::uint8_t, const orbit::SceneAttribute&) { return true; }

orbit::SceneLifecyclePort lifecycle() {
  orbit::SceneLifecyclePort port{};
  port.is_stopped = lc_true;
  port.stop_release_drain = lc_void;
  port.request_quiesce = lc_quiesce;
  port.quiesced = lc_quiesced;
  port.quiesce_timed_out = lc_timed_out;
  port.cancel_quiesce = lc_cancel;
  port.resume = lc_resume;
  port.capture = lc_capture;
  port.reset_defaults = lc_true;
  port.prepare_scene = lc_true;
  port.apply_globals = lc_globals;
  port.construct_module = lc_construct;
  port.apply_parameter = lc_param;
  port.apply_attribute = lc_attr;
  port.finish_scene = lc_true;
  port.rebuild_ui = lc_void;
  return port;
}

orbit::SceneServiceStatus run(orbit::SceneService& service) {
  std::uint32_t now = 0;
  for (int i = 0; i < 100 && service.busy(); ++i) service.poll(now++);
  return service.status();
}

std::size_t build_frame(char type, const char* name, std::size_t name_length,
                        const std::uint8_t* payload, std::uint32_t payload_length,
                        std::uint8_t* out) {
  out[0] = 0x4F; out[1] = 0x31; out[2] = static_cast<std::uint8_t>(type);
  out[3] = static_cast<std::uint8_t>(name_length);
  for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<std::uint8_t>(payload_length >> (8 * i));
  std::size_t n = 8;
  std::memcpy(out + n, name, name_length);
  n += name_length;
  std::memcpy(out + n, payload, payload_length);
  n += payload_length;
  std::uint32_t crc = mt::crc32_init();
  for (std::size_t i = 0; i < n; ++i) crc = mt::crc32_update(crc, out[i]);
  crc = mt::crc32_final(crc);
  for (int i = 0; i < 4; ++i) out[n++] = static_cast<std::uint8_t>(crc >> (8 * i));
  return n;
}

// Encodes a valid scene whose BPM is `bpm`, so two scene texts differ.
std::size_t scene_text(std::uint16_t bpm, char* text, std::size_t capacity) {
  orbit::SceneSchemaResolver schema{nullptr, schema_lookup};
  orbit::SceneDto dto{};
  dto.valid = true;
  dto.globals.bpm = bpm;
  dto.global_fields = orbit::SceneBpm;
  std::size_t written = 0;
  const auto result = orbit::encode_scene(dto, schema, text, capacity, written);
  if (result != orbit::SceneResult::Ok) {
    std::fprintf(stderr, "encode_scene failed: %d\n", static_cast<int>(result));
    ++failures;
  }
  return written;
}

// Feeds one PUT through a session on the fake filesystem; returns whether it was ACKed.
bool put_frame(t13::Fs& fs, const orbit::SceneSchemaResolver& schema, const char* name,
               std::size_t name_length, const std::uint8_t* payload, std::uint32_t length) {
  orbit::SceneService service(t13::port(fs), lifecycle(), schema);
  struct Gate { bool stopped = true; } gate;
  auto gate_stopped = [](void* context) { return static_cast<Gate*>(context)->stopped; };
  struct Sink { std::uint32_t frames = 0; } sink;
  auto sink_write = [](void*, const std::uint8_t*, std::size_t) { return true; };
  mt::MaintenanceSession session(t13::port(fs), schema, service, {&sink, sink_write},
                                 {&gate, gate_stopped});
  static std::uint8_t frame[64 + orbit::kSceneMaxBytes];
  const std::size_t n = build_frame('P', name, name_length, payload, length, frame);
  session.feed(0);  // a stray byte before sync must be ignored
  for (std::size_t i = 0; i < n; ++i) session.feed(frame[i]);
  return session.counters().accepted == 1;
}

// Sweeps every power-cut point of a PUT. `old_bytes` is the prior file, `new_bytes` the payload.
void sweep(SceneFileId target, const char* name, std::size_t name_length,
           const char* old_bytes, std::size_t old_length, const char* new_bytes,
           std::size_t new_length, bool scene) {
  orbit::SceneSchemaResolver schema{nullptr, schema_lookup};
  const auto life = lifecycle();
  int committed_cuts = 0;
  int cut_points = 0;
  for (int cut = 0; cut < 12; ++cut) {
    auto& fs = t13::shared_fs();
    fs.reset();
    fs.put(target, "");
    std::memcpy(fs.cell(target).bytes, old_bytes, old_length);
    fs.cell(target).size = old_length;
    fs.cut_after = cut;
    const bool acked = put_frame(fs, schema, name, name_length,
                                 reinterpret_cast<const std::uint8_t*>(new_bytes),
                                 static_cast<std::uint32_t>(new_length));
    ++cut_points;
    fs.reboot();

    // Boot-time recovery: loading the target runs the same recovery as SceneService.
    orbit::SceneService loader(t13::port(fs), life, schema);
    bool loaded = false;
    if (scene) {
      loaded = loader.request(orbit::SceneOperation::Load, 0) && run(loader) == orbit::SceneServiceStatus::Succeeded;
    } else {
      loaded = loader.request(orbit::SceneOperation::LoadUserSettings) &&
               run(loader) == orbit::SceneServiceStatus::Succeeded;
    }
    const bool is_old = fs.cell(target).exists && fs.cell(target).size == old_length &&
                        std::memcmp(fs.cell(target).bytes, old_bytes, old_length) == 0;
    const bool is_new = fs.cell(target).exists && fs.cell(target).size == new_length &&
                        std::memcmp(fs.cell(target).bytes, new_bytes, new_length) == 0;
    if (!loaded || !(is_old || is_new)) {
      std::fprintf(stderr, "%s cut %d: loaded=%d old=%d new=%d exists=%d size=%zu\n", name, cut,
                   loaded, is_old, is_new, fs.cell(target).exists, fs.cell(target).size);
      CHECK(false);
    }
    // An acknowledged PUT must have stored the new content.
    if (acked) {
      CHECK(is_new);
      ++committed_cuts;
    }
  }
  // The sweep must include both interrupted and completed transfers.
  CHECK(committed_cuts > 0);
  CHECK(cut_points == 12);
}

void test_user_settings_power_cut_sweep() {
  const char old_text[] = "key_brightness,0.2\n";
  const char new_text[] = "key_brightness,0.5\n";
  sweep({SceneFileKind::UserSettings, 0}, "user_settings", 13, old_text, sizeof(old_text) - 1,
        new_text, sizeof(new_text) - 1, false);
}

void test_scene_power_cut_sweep() {
  char old_text[orbit::kSceneMaxBytes];
  char new_text[orbit::kSceneMaxBytes];
  const std::size_t old_length = scene_text(120, old_text, sizeof(old_text));
  const std::size_t new_length = scene_text(90, new_text, sizeof(new_text));
  CHECK(old_length > 0 && new_length > 0);
  CHECK(old_length != new_length || std::memcmp(old_text, new_text, old_length) != 0);
  sweep({SceneFileKind::Scene, 0}, "Scene01", 7, old_text, old_length, new_text, new_length, true);
}

}  // namespace

int main() {
  test_user_settings_power_cut_sweep();
  test_scene_power_cut_sweep();
  if (failures != 0) {
    std::fprintf(stderr, "t13c_interrupted: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("t13c_interrupted: all checks passed");
  return 0;
}
