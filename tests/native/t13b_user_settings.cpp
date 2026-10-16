// T13 user settings (KEYBRI): text parse/encode rules and SceneService
// save/load with failure injection and a power-cut sweep over every mutating
// storage operation. Source: apps/Orion/core/user_settings.py.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "persistence/scene_service.hpp"
#include "persistence/user_settings.hpp"
#include "t13_fs.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

using namespace orbit;

UserSettingsResult parse_text(const char* text, UserSettings& settings) {
  return parse_user_settings(text, std::strlen(text), settings);
}

void test_parse_rules() {
  UserSettings s{};
  CHECK(parse_text("key_brightness,0.5\n", s) == UserSettingsResult::Ok && s.key_brightness == 0.5f);
  s = {};
  CHECK(parse_text("key_brightness,1\r\n", s) == UserSettingsResult::Ok && s.key_brightness == 1.0f);
  s = {};
  CHECK(parse_text("key_brightness,0.25", s) == UserSettingsResult::Ok && s.key_brightness == 0.25f);
  s = {};
  // The name keeps its inner space, so it is an unknown key (as in the Python loader).
  CHECK(parse_text(" key_brightness , 0.5 \n", s) == UserSettingsResult::Ok && s.key_brightness == kUserKeyBrightnessDefault);
  s = {};
  CHECK(parse_text("unknown,7\nkey_brightness,0.75\n", s) == UserSettingsResult::Ok &&
        s.key_brightness == 0.75f);
  s = {};
  CHECK(parse_text("key_brightness,1.5\n", s) == UserSettingsResult::OutOfRange);
  CHECK(parse_text("key_brightness,-0.1\n", s) == UserSettingsResult::OutOfRange);
  // Python float text written without rounding (str() of binary noise) is accepted.
  s = {};
  CHECK(parse_text("key_brightness,0.30000000000000004\n", s) == UserSettingsResult::Ok &&
        std::fabs(s.key_brightness - 0.3f) < 1e-6f);
  s = {};
  CHECK(parse_text("key_brightness,2.7755575615628914e-17\n", s) == UserSettingsResult::Ok &&
        s.key_brightness == 0.0f);
  s = {};
  CHECK(parse_text("key_brightness,-0.0\n", s) == UserSettingsResult::Ok && s.key_brightness == 0.0f);
  s = {};
  CHECK(parse_text("key_brightness,1e-1\n", s) == UserSettingsResult::Ok &&
        std::fabs(s.key_brightness - 0.1f) < 1e-6f);
  s = {};
  CHECK(parse_text("key_brightness,1.5e-1\n", s) == UserSettingsResult::Ok &&
        std::fabs(s.key_brightness - 0.15f) < 1e-6f);
  s = {};
  CHECK(parse_text("key_brightness,1.0000000000000002\n", s) == UserSettingsResult::Ok &&
        s.key_brightness == 1.0f);
  CHECK(parse_text("key_brightness,1.0000005\n", s) == UserSettingsResult::OutOfRange);
  CHECK(parse_text("key_brightness,1e5\n", s) == UserSettingsResult::OutOfRange);
  CHECK(parse_text("key_brightness,0x1\n", s) == UserSettingsResult::Malformed);
  CHECK(parse_text("key_brightness,1e\n", s) == UserSettingsResult::Malformed);
  CHECK(parse_text("key_brightness,abc\n", s) == UserSettingsResult::Malformed);
  CHECK(parse_text("key_brightness,nan\n", s) == UserSettingsResult::Malformed);
  CHECK(parse_text("key_brightness,\n", s) == UserSettingsResult::Malformed);
  CHECK(parse_text("no_comma_here\n", s) == UserSettingsResult::Malformed);
  // A rejected value leaves the output untouched.
  s.key_brightness = 0.4f;
  CHECK(parse_text("key_brightness,2\n", s) != UserSettingsResult::Ok && s.key_brightness == 0.4f);
  // Oversize text is refused before parsing.
  char big[kUserSettingsMaxBytes + 2];
  std::memset(big, '\n', sizeof(big));
  CHECK(parse_user_settings(big, sizeof(big), s) == UserSettingsResult::TooLarge);
  // Missing key keeps the current value (source: default is left unchanged).
  s.key_brightness = 0.9f;
  CHECK(parse_text("other,1\n", s) == UserSettingsResult::Ok && s.key_brightness == 0.9f);
}

void test_encode_round_trip() {
  char text[64];
  std::size_t written = 0;
  UserSettings s{0.3f};
  CHECK(encode_user_settings(s, text, sizeof(text), written) == UserSettingsResult::Ok);
  CHECK(written == std::strlen("key_brightness,0.3\n") && std::memcmp(text, "key_brightness,0.3\n", written) == 0);
  s.key_brightness = 1.0f;
  CHECK(encode_user_settings(s, text, sizeof(text), written) == UserSettingsResult::Ok);
  CHECK(std::memcmp(text, "key_brightness,1\n", written) == 0);
  s.key_brightness = 0.0f;
  CHECK(encode_user_settings(s, text, sizeof(text), written) == UserSettingsResult::Ok);
  CHECK(std::memcmp(text, "key_brightness,0\n", written) == 0);
  s.key_brightness = 7.0f;   // out-of-range values are clamped on write
  CHECK(encode_user_settings(s, text, sizeof(text), written) == UserSettingsResult::Ok);
  CHECK(std::memcmp(text, "key_brightness,1\n", written) == 0);
  s.key_brightness = std::nanf("");
  CHECK(encode_user_settings(s, text, sizeof(text), written) == UserSettingsResult::Ok);
  CHECK(std::memcmp(text, "key_brightness,0\n", written) == 0);
  CHECK(encode_user_settings(UserSettings{0.3f}, text, 4, written) == UserSettingsResult::OutputFull);

  // Round trip within 1e-6 for a grid of brightness values.
  for (int step = 0; step <= 20; ++step) {
    const float value = static_cast<float>(step) * 0.05f;
    UserSettings encoded{value};
    CHECK(encode_user_settings(encoded, text, sizeof(text), written) == UserSettingsResult::Ok);
    UserSettings decoded{-1.0f};
    CHECK(parse_user_settings(text, written, decoded) == UserSettingsResult::Ok);
    CHECK(std::fabs(decoded.key_brightness - value) < 1e-6f);
  }
}

// --- SceneService with a fake lifecycle --------------------------------------

bool lc_true(void*) { return true; }
void lc_void(void*) {}
std::uint32_t lc_quiesce(void*, std::uint32_t, std::uint32_t) { return 1; }
bool lc_quiesced(void*, std::uint32_t) { return true; }
bool lc_timed_out(void*, std::uint32_t, std::uint32_t) { return false; }
void lc_cancel(void*, std::uint32_t) {}
void lc_resume(void*, std::uint32_t) {}
bool lc_capture(void*, SceneDto&) { return true; }
bool lc_globals(void*, const GlobalState&, std::uint8_t) { return true; }
bool lc_construct(void*, std::uint8_t, const char*, std::uint16_t, bool) { return true; }
bool lc_param(void*, std::uint8_t, const SceneParameter&) { return true; }
bool lc_attr(void*, std::uint8_t, const SceneAttribute&) { return true; }
bool lc_schema_ok(void*, std::uint8_t, const char*, SceneModuleSchema&) { return false; }

SceneLifecyclePort lifecycle() {
  SceneLifecyclePort port{};
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

// Runs one request to completion (or RecoveryRequired/failure) and returns the final status.
SceneServiceStatus run(SceneService& service) {
  std::uint32_t now = 0;
  for (int i = 0; i < 100 && service.busy(); ++i) service.poll(now++);
  return service.status();
}

constexpr SceneFileId kUser{SceneFileKind::UserSettings, 0};

void test_save_load_round_trip() {
  auto& fs = t13::shared_fs();
  fs.reset();
  SceneSchemaResolver schema{nullptr, lc_schema_ok};
  const auto life = lifecycle();
  {
    SceneService service(t13::port(fs), life, schema);
    CHECK(service.set_user_settings(UserSettings{0.6f}));
    CHECK(service.request(SceneOperation::SaveUserSettings));
    CHECK(run(service) == SceneServiceStatus::Succeeded);
    CHECK(fs.equals(kUser, "key_brightness,0.6\n"));
    CHECK(!fs.cell({SceneFileKind::UserSettingsTemp, 0}).exists);
    CHECK(!fs.cell({SceneFileKind::UserSettingsBackup, 0}).exists);
  }
  {
    SceneService service(t13::port(fs), life, schema);
    CHECK(service.request(SceneOperation::LoadUserSettings));
    CHECK(run(service) == SceneServiceStatus::Succeeded);
    CHECK(service.user_settings_present());
    CHECK(std::fabs(service.user_settings().key_brightness - 0.6f) < 1e-6f);
  }
  // Missing file: defaults (1.0) and no file is created by a load.
  fs.reset();
  {
    SceneService service(t13::port(fs), life, schema);
    CHECK(service.request(SceneOperation::LoadUserSettings));
    CHECK(run(service) == SceneServiceStatus::Succeeded);
    CHECK(!service.user_settings_present());
    CHECK(service.user_settings().key_brightness == kUserKeyBrightnessDefault);
    CHECK(!fs.cell(kUser).exists);
  }
}

void test_invalid_file_is_rejected_not_silently_replaced() {
  auto& fs = t13::shared_fs();
  fs.reset();
  fs.put(kUser, "key_brightness,9\n");
  SceneSchemaResolver schema{nullptr, lc_schema_ok};
  const auto life = lifecycle();
  SceneService service(t13::port(fs), life, schema);
  CHECK(service.request(SceneOperation::LoadUserSettings));
  CHECK(run(service) == SceneServiceStatus::SceneError);
  CHECK(service.codec_result() == SceneResult::InvalidValue);
  CHECK(service.user_settings().key_brightness == kUserKeyBrightnessDefault);
  CHECK(!service.busy());
}

void test_failed_save_keeps_previous_file() {
  SceneSchemaResolver schema{nullptr, lc_schema_ok};
  const auto life = lifecycle();
  {
    auto& fs = t13::shared_fs();
    fs.reset();
    fs.put(kUser, "key_brightness,0.2\n");
    fs.fail_open_write = true;
    SceneService service(t13::port(fs), life, schema);
    service.set_user_settings(UserSettings{0.8f});
    CHECK(service.request(SceneOperation::SaveUserSettings));
    CHECK(run(service) == SceneServiceStatus::StorageError);
    CHECK(fs.equals(kUser, "key_brightness,0.2\n"));
  }
  {
    auto& fs = t13::shared_fs();
    fs.reset();
    fs.put(kUser, "key_brightness,0.2\n");
    fs.free_bytes = 4;  // write refused mid-file
    SceneService service(t13::port(fs), life, schema);
    service.set_user_settings(UserSettings{0.8f});
    CHECK(service.request(SceneOperation::SaveUserSettings));
    CHECK(run(service) == SceneServiceStatus::StorageError);
    CHECK(fs.equals(kUser, "key_brightness,0.2\n"));
    CHECK(!fs.cell({SceneFileKind::UserSettingsTemp, 0}).exists);
  }
}

// Power cut after each mutating operation of a save. After reboot, a load must
// return either the old or the new value, never an error or a corrupt value.
void test_power_cut_sweep_never_corrupts_settings() {
  SceneSchemaResolver schema{nullptr, lc_schema_ok};
  const auto life = lifecycle();
  int cuts_with_effect = 0;
  for (int cut = 0; cut < 12; ++cut) {
    auto& fs = t13::shared_fs();
    fs.reset();
    fs.put(kUser, "key_brightness,0.2\n");
    fs.cut_after = cut;
    {
      SceneService service(t13::port(fs), life, schema);
      service.set_user_settings(UserSettings{0.7f});
      service.request(SceneOperation::SaveUserSettings);
      run(service);
    }
    fs.reboot();
    SceneService loader(t13::port(fs), life, schema);
    CHECK(loader.request(SceneOperation::LoadUserSettings));
    const SceneServiceStatus status = run(loader);
    const bool old_value = std::fabs(loader.user_settings().key_brightness - 0.2f) < 1e-6f;
    const bool new_value = std::fabs(loader.user_settings().key_brightness - 0.7f) < 1e-6f;
    if (!(status == SceneServiceStatus::Succeeded && (old_value || new_value))) {
      std::fprintf(stderr, "cut %d: status=%d value=%f\n", cut, static_cast<int>(status),
                   static_cast<double>(loader.user_settings().key_brightness));
      CHECK(false);
    }
    if (new_value) ++cuts_with_effect;
  }
  // Cutting after the commit must expose the new value.
  CHECK(cuts_with_effect > 0);
}

}  // namespace

int main() {
  test_parse_rules();
  test_encode_round_trip();
  test_save_load_round_trip();
  test_invalid_file_is_rejected_not_silently_replaced();
  test_failed_save_keeps_previous_file();
  test_power_cut_sweep_never_corrupts_settings();
  if (failures != 0) {
    std::fprintf(stderr, "t13b_user_settings: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("t13b_user_settings: all checks passed");
  return 0;
}
