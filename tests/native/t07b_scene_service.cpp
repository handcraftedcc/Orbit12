#include <cstdint>
#include <cstdio>
#include <cstring>

#include "persistence/scene_service.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

constexpr std::size_t kKinds = static_cast<std::size_t>(orbit::SceneFileKind::SceneNamesBackup) + 1;
constexpr std::size_t kScenes = orbit::kSceneSlotCount;
constexpr const char* kDefaultNames = "01,ALFA\n02,BRAVO\n03,CHARLIE\n10,JULIET\n";
struct Cell { char bytes[orbit::kSceneMaxBytes]{}; std::size_t size = 0; bool exists = false; };
struct MemoryFs;
struct ReadCursor { MemoryFs* fs = nullptr; orbit::SceneFileId id{}; std::size_t pos = 0; };
struct WriteCursor { MemoryFs* fs = nullptr; orbit::SceneFileId id{}; };
struct MemoryFs {
  Cell files[kKinds][kScenes]{};
  ReadCursor reader{};
  WriteCursor writer{};
  std::uint32_t storage_calls = 0;
  bool fail_open_write = false;
  int open_write_budget = -1;  // successful open_write calls left before failing (-1: unlimited)
  bool fail_write = false;
  bool fail_close_temp = false;
  bool fail_scene_install = false;
  bool fail_pointer_install = false;
  bool fail_rename_to_backup = false;
};

Cell& cell(MemoryFs& fs, orbit::SceneFileId id) {
  return fs.files[static_cast<std::uint8_t>(id.kind)][id.scene];
}
void seed_text(MemoryFs& fs, orbit::SceneFileId id, const char* text) {
  auto& file = cell(fs, id);
  file.exists = true;
  file.size = std::strlen(text);
  std::memcpy(file.bytes, text, file.size);
}
bool text_is(MemoryFs& fs, orbit::SceneFileId id, const char* text) {
  auto& file = cell(fs, id);
  return file.exists && file.size == std::strlen(text) &&
         std::memcmp(file.bytes, text, file.size) == 0;
}
// Resets in place: a whole-struct temporary (about 1.6 MB) would overflow the stack.
void reset_fs(MemoryFs& fs) {
  for (auto& kind : fs.files)
    for (auto& file : kind) {
      file.size = 0;
      file.exists = false;
    }
  fs.reader = {};
  fs.writer = {};
  fs.storage_calls = 0;
  fs.fail_open_write = false;
  fs.open_write_budget = -1;
  fs.fail_write = false;
  fs.fail_close_temp = false;
  fs.fail_scene_install = false;
  fs.fail_pointer_install = false;
  fs.fail_rename_to_backup = false;
  // Every fixture starts with named slots 1, 2, 3 and 10 (Scene01, Scene02, Scene03, Scene10).
  seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames);
}
orbit::SceneStorageStatus fs_exists(void* context, orbit::SceneFileId id, bool& exists) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  exists = cell(fs, id).exists;
  return orbit::SceneStorageStatus::Ok;
}
orbit::SceneReadStatus fs_read(void* context, char& byte) {
  auto& cursor = *static_cast<ReadCursor*>(context);
  auto& source = cell(*cursor.fs, cursor.id);
  if (cursor.pos >= source.size) return orbit::SceneReadStatus::End;
  byte = source.bytes[cursor.pos++];
  return orbit::SceneReadStatus::Byte;
}
orbit::SceneWriteStatus fs_write(void* context, const char* data, std::size_t size) {
  auto& cursor = *static_cast<WriteCursor*>(context);
  auto& fs = *cursor.fs;
  auto& target = cell(fs, cursor.id);
  if (fs.fail_write) return orbit::SceneWriteStatus::Error;
  if (size > sizeof(target.bytes) - target.size) return orbit::SceneWriteStatus::Full;
  std::memcpy(target.bytes + target.size, data, size);
  target.size += size;
  return orbit::SceneWriteStatus::Written;
}
orbit::SceneStorageStatus fs_open_read(void* context, orbit::SceneFileId id,
                                       orbit::SceneReader& reader) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  if (!cell(fs, id).exists) return orbit::SceneStorageStatus::Missing;
  fs.reader = {&fs, id, 0};
  reader = {&fs.reader, fs_read};
  return orbit::SceneStorageStatus::Ok;
}
orbit::SceneStorageStatus fs_open_write(void* context, orbit::SceneFileId id,
                                        orbit::SceneWriter& writer) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  if (fs.fail_open_write) return orbit::SceneStorageStatus::Error;
  if (fs.open_write_budget == 0) return orbit::SceneStorageStatus::Error;
  if (fs.open_write_budget > 0) --fs.open_write_budget;
  auto& target = cell(fs, id);
  target.exists = true;
  target.size = 0;
  fs.writer = {&fs, id};
  writer = {&fs.writer, fs_write};
  return orbit::SceneStorageStatus::Ok;
}
orbit::SceneStorageStatus fs_close(void* context, orbit::SceneFileId id) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  if (fs.fail_close_temp && id.kind == orbit::SceneFileKind::SceneTemp) {
    fs.fail_close_temp = false;
    return orbit::SceneStorageStatus::Error;
  }
  return orbit::SceneStorageStatus::Ok;
}
orbit::SceneStorageStatus fs_rename(void* context, orbit::SceneFileId from,
                                    orbit::SceneFileId to) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  if (fs.fail_rename_to_backup &&
      (to.kind == orbit::SceneFileKind::SceneBackup || to.kind == orbit::SceneFileKind::ActiveBackup)) {
    fs.fail_rename_to_backup = false;
    return orbit::SceneStorageStatus::Error;
  }
  if (fs.fail_scene_install && from.kind == orbit::SceneFileKind::SceneTemp &&
      to.kind == orbit::SceneFileKind::Scene) {
    fs.fail_scene_install = false;
    return orbit::SceneStorageStatus::Error;
  }
  if (fs.fail_pointer_install && from.kind == orbit::SceneFileKind::ActiveTemp &&
      to.kind == orbit::SceneFileKind::ActivePointer) {
    fs.fail_pointer_install = false;
    return orbit::SceneStorageStatus::Error;
  }
  auto& src = cell(fs, from);
  auto& dst = cell(fs, to);
  if (!src.exists || dst.exists) return orbit::SceneStorageStatus::Error;
  std::memcpy(dst.bytes, src.bytes, src.size);
  dst.size = src.size;
  dst.exists = true;
  src.exists = false;
  src.size = 0;
  return orbit::SceneStorageStatus::Ok;
}
orbit::SceneStorageStatus fs_remove(void* context, orbit::SceneFileId id) {
  auto& fs = *static_cast<MemoryFs*>(context);
  ++fs.storage_calls;
  auto& target = cell(fs, id);
  if (!target.exists) return orbit::SceneStorageStatus::Missing;
  target.exists = false;
  target.size = 0;
  return orbit::SceneStorageStatus::Ok;
}

bool schema_lookup(void*, std::uint8_t slot, const char* id, orbit::SceneModuleSchema& out) {
  if (slot >= 2 && slot <= 7 && std::strcmp(id, "empty") == 0) {
    out = {"empty", 1, orbit::SceneModuleKind::Processor, nullptr, 0, nullptr, 0};
    return true;
  }
  return false;
}
const orbit::SceneSchemaResolver kSchema{nullptr, schema_lookup};

struct LiveState {
  bool stopped = false;
  bool worker_ack = false;
  bool fail_apply = false;
  std::uint32_t sequence = 0;
  std::uint32_t resumes = 0;
  std::uint32_t captures = 0;
  std::uint32_t applied_bpm = 0;
  std::uint32_t order = 0;
  std::uint32_t global_order = 0;
  std::uint32_t construct_order = 0;
};
bool is_stopped(void* context) { return static_cast<LiveState*>(context)->stopped; }
void stop_release(void* context) { static_cast<LiveState*>(context)->stopped = true; }
std::uint32_t request_quiesce(void* context, std::uint32_t, std::uint32_t) {
  auto& live = *static_cast<LiveState*>(context);
  return ++live.sequence;
}
bool quiesced(void* context, std::uint32_t) { return static_cast<LiveState*>(context)->worker_ack; }
bool timed_out(void*, std::uint32_t, std::uint32_t now) { return now > 100; }
void cancel(void* context, std::uint32_t) { static_cast<LiveState*>(context)->worker_ack = false; }
void resume(void* context, std::uint32_t) {
  auto& live = *static_cast<LiveState*>(context);
  ++live.resumes;
  live.worker_ack = false;
}
bool capture(void* context, orbit::SceneDto& dto) {
  auto& live = *static_cast<LiveState*>(context);
  ++live.captures;
  dto.global_fields = orbit::SceneBpm;
  dto.globals.bpm = static_cast<std::uint16_t>(live.applied_bpm ? live.applied_bpm : 140);
  return true;
}
bool reset_defaults(void* context) {
  auto& live = *static_cast<LiveState*>(context);
  live.applied_bpm = 120;
  return true;
}
bool prepare_scene(void* context) {
  auto& live = *static_cast<LiveState*>(context);
  live.order = 0;
  live.global_order = 0;
  live.construct_order = 0;
  return true;
}
bool apply_globals(void* context, const orbit::GlobalState& globals, std::uint8_t fields) {
  auto& live = *static_cast<LiveState*>(context);
  if (live.fail_apply) return false;   // fails the target apply and the rollback apply alike
  if (fields & orbit::SceneBpm) live.applied_bpm = globals.bpm;
  live.global_order = ++live.order;
  return true;
}
bool construct_module(void* context, std::uint8_t, const char*, std::uint16_t, bool) {
  auto& live = *static_cast<LiveState*>(context);
  live.construct_order = ++live.order;
  return true;
}
bool apply_parameter(void* context, std::uint8_t, const orbit::SceneParameter&) {
  return !static_cast<LiveState*>(context)->fail_apply;
}
bool apply_attribute(void*, std::uint8_t, const orbit::SceneAttribute&) { return true; }
bool finish_scene(void*) { return true; }
void rebuild(void*) {}

orbit::SceneStoragePort storage_port(MemoryFs& fs) {
  return {&fs, fs_exists, fs_open_read, fs_open_write, fs_close,
          fs_rename, fs_remove};
}
orbit::SceneLifecyclePort lifecycle_port(LiveState& live) {
  return {&live, is_stopped, stop_release, request_quiesce, quiesced, timed_out,
          cancel, resume, capture, reset_defaults, prepare_scene, apply_globals,
          construct_module, apply_parameter, apply_attribute, finish_scene, rebuild};
}

void put_scene(MemoryFs& fs, std::uint8_t scene, std::uint16_t bpm, bool with_module = false) {
  orbit::SceneDto dto{};
  dto.valid = true;
  dto.global_fields = orbit::SceneBpm;
  dto.globals.bpm = bpm;
  if (with_module) {
    auto& module = dto.modules[2];
    module.present = true;
    std::memcpy(module.id, "empty", sizeof("empty"));
    module.version = 1;
  }
  auto& target = cell(fs, {orbit::SceneFileKind::Scene, scene});
  CHECK(orbit::encode_scene(dto, kSchema, target.bytes, sizeof(target.bytes), target.size) == orbit::SceneResult::Ok);
  target.exists = true;
}
std::uint16_t scene_bpm(MemoryFs& fs, std::uint8_t scene) {
  auto& source = cell(fs, {orbit::SceneFileKind::Scene, scene});
  orbit::SceneDto dto{};
  const auto status = orbit::decode_scene(source.bytes, source.size, kSchema, dto);
  CHECK(status == orbit::SceneResult::Ok);
  return dto.globals.bpm;
}
bool pointer_is(MemoryFs& fs, const char* expected) {
  auto& pointer = cell(fs, {orbit::SceneFileKind::ActivePointer, 0});
  const auto length = std::strlen(expected);
  return pointer.exists && pointer.size == length && std::memcmp(pointer.bytes, expected, length) == 0;
}
void seed_pointer(MemoryFs& fs, const char* text) {
  auto& ptr = cell(fs, {orbit::SceneFileKind::ActivePointer, 0});
  ptr.exists = true;
  ptr.size = std::strlen(text);
  std::memcpy(ptr.bytes, text, ptr.size);
}
orbit::SceneServiceStatus complete(orbit::SceneService& service, LiveState& live,
                                   std::uint32_t start = 0) {
  for (std::uint32_t i = 0; i < 100 && service.busy(); ++i) {
    service.poll(start + i);
    if (service.phase() == orbit::SceneServicePhase::WaitQuiesce ||
        service.phase() == orbit::SceneServicePhase::RecoveryWait) live.worker_ack = true;
  }
  CHECK(!service.busy());
  return service.status();
}

// Boot names load. Clears the resume/storage counters it touches so each test starts clean.
orbit::SceneServiceStatus load_names(orbit::SceneService& service, LiveState& live,
                                     MemoryFs& fs) {
  CHECK(service.request(orbit::SceneOperation::LoadNames));
  const auto status = complete(service, live);
  live.resumes = 0;
  fs.storage_calls = 0;
  return status;
}

// Polls until idle or parked in Failed (which stays busy by design).
orbit::SceneServiceStatus settle(orbit::SceneService& service, LiveState& live,
                                 std::uint32_t start = 0) {
  for (std::uint32_t i = 0; i < 100 && service.busy() && service.phase() != orbit::SceneServicePhase::Failed; ++i) {
    service.poll(start + i);
    if (service.phase() == orbit::SceneServicePhase::WaitQuiesce ||
        service.phase() == orbit::SceneServicePhase::RecoveryWait) live.worker_ack = true;
  }
  return service.status();
}

void switch_and_copy_transaction() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  put_scene(fs, 1, 90, true);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);

  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  service.poll(0); service.poll(1);
  CHECK(service.phase() == orbit::SceneServicePhase::WaitQuiesce);
  CHECK(fs.storage_calls == 0); // No flash access before core-1 acknowledgement.
  live.worker_ack = true;
  const auto switched = complete(service, live, 2);
  CHECK(switched == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 1 && pointer_is(fs, "2\n"));
  CHECK(live.applied_bpm == 90 && live.global_order < live.construct_order);
  CHECK(scene_bpm(fs, 0) == 140); // Current live scene was durably saved first.
  CHECK(live.resumes == 1);

  CHECK(service.request(orbit::SceneOperation::CopyCurrent, 2));
  const auto copied = complete(service, live, 20);
  CHECK(copied == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 2 && pointer_is(fs, "3\n"));
  CHECK(scene_bpm(fs, 2) == 90);
}

void file_and_pointer_failures_restore() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  put_scene(fs, 1, 80);
  put_scene(fs, 2, 60);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 135;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);

  fs.fail_scene_install = true; // target->backup succeeds, temp install fails, backup is restored.
  CHECK(service.request(orbit::SceneOperation::CopyCurrent, 1));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::StorageError);
  CHECK(scene_bpm(fs, 1) == 80 && pointer_is(fs, "1\n"));

  fs.fail_pointer_install = true;
  CHECK(service.request(orbit::SceneOperation::Switch, 2));
  CHECK(complete(service, live, 20) == orbit::SceneServiceStatus::StorageError);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.applied_bpm == 135 && scene_bpm(fs, 0) == 135);

  fs.fail_open_write = true;
  const auto before = scene_bpm(fs, 0);
  CHECK(service.request(orbit::SceneOperation::Save));
  CHECK(complete(service, live, 40) == orbit::SceneServiceStatus::StorageError);
  CHECK(scene_bpm(fs, 0) == before && pointer_is(fs, "1\n"));
}

void recovery_pointer_and_reset() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 3, 77);
  cell(fs, {orbit::SceneFileKind::SceneBackup, 3}).exists = true;
  cell(fs, {orbit::SceneFileKind::SceneBackup, 3}).size = cell(fs, {orbit::SceneFileKind::Scene, 3}).size;
  std::memcpy(cell(fs, {orbit::SceneFileKind::SceneBackup, 3}).bytes,
              cell(fs, {orbit::SceneFileKind::Scene, 3}).bytes,
              cell(fs, {orbit::SceneFileKind::Scene, 3}).size);
  cell(fs, {orbit::SceneFileKind::Scene, 3}).exists = false;
  seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, "01,ALFA\n02,BRAVO\n03,CHARLIE\n04,DELTA\n10,JULIET\n");
  seed_pointer(fs, "4\r\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request(orbit::SceneOperation::LoadActive));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 3 && live.applied_bpm == 77);
  CHECK(cell(fs, {orbit::SceneFileKind::Scene, 3}).exists);

  live.applied_bpm = 88;
  CHECK(service.request(orbit::SceneOperation::Reset));
  CHECK(complete(service, live, 20) == orbit::SceneServiceStatus::Succeeded);
  CHECK(live.applied_bpm == 120 && scene_bpm(fs, 3) == 120);
}

// Forced rollback failure, then in-place recovery. The staged apply fails for the
// target and again for the rollback; the service then stays Failed. Entering Failed
// releases the worker (resume) so its recovery gesture is scanned; acknowledge_recovery()
// requests a new quiesce, re-reads the committed scene and re-applies it in place once
// the fault is gone. Nothing is reset to defaults on the way.
void recovery_after_rollback_failure() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  put_scene(fs, 1, 90, true);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);

  CHECK(!service.acknowledge_recovery());          // not Failed: nothing to acknowledge
  live.fail_apply = true;
  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  CHECK(settle(service, live) == orbit::SceneServiceStatus::RecoveryRequired);
  CHECK(service.phase() == orbit::SceneServicePhase::Failed && service.busy());
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.resumes == 1);                        // Failed released the worker (once)
  CHECK(service.failure_count() == 1);
  CHECK(scene_bpm(fs, 0) == 140);                  // the live scene was saved before the switch
  CHECK(!service.request(orbit::SceneOperation::Save));  // refused while Failed
  for (std::uint32_t i = 0; i < 5; ++i) service.poll(i);
  CHECK(service.phase() == orbit::SceneServicePhase::Failed);

  // Re-apply still fails: stays Failed and reports the error.
  CHECK(service.acknowledge_recovery());
  CHECK(settle(service, live, 0) == orbit::SceneServiceStatus::RecoveryRequired);
  CHECK(service.phase() == orbit::SceneServicePhase::Failed && live.resumes == 2);
  CHECK(service.failure_count() == 2);             // each failed recovery is counted

  // Fault cleared: the committed scene is re-applied in place and the service resumes.
  live.fail_apply = false;
  live.applied_bpm = 0;
  CHECK(service.acknowledge_recovery());
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::Succeeded);
  CHECK(!service.busy() && live.resumes == 3);
  CHECK(service.failure_count() == 2);             // success is not a failure
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.applied_bpm == 140);                  // scene 0 contents, not reset defaults (120)
  CHECK(!service.acknowledge_recovery());          // idle again
}

// Recovery read failure keeps the service Failed (no apply attempted, no fabricated success).
void recovery_read_failure_stays_failed() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  put_scene(fs, 1, 90, true);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  live.fail_apply = true;
  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  CHECK(settle(service, live) == orbit::SceneServiceStatus::RecoveryRequired);

  auto& committed = cell(fs, {orbit::SceneFileKind::Scene, 0});
  const bool was_present = committed.exists;
  committed.exists = false;                        // committed scene unreadable
  live.fail_apply = false;
  CHECK(service.acknowledge_recovery());
  CHECK(settle(service, live, 0) == orbit::SceneServiceStatus::RecoveryRequired);
  CHECK(service.phase() == orbit::SceneServicePhase::Failed && live.resumes == 2);
  committed.exists = was_present;
  CHECK(service.acknowledge_recovery());
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::Succeeded);
  CHECK(live.resumes == 3 && live.applied_bpm == 140);
}

// A Reset whose save cannot open the temp file reports the storage error (not ApplyError),
// and the rollback restores the committed scene in place.
void reset_save_failure_reports_storage_error() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 90);
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  fs.open_write_budget = 1;                        // SaveCurrent writes; SaveReset cannot open
  CHECK(service.request(orbit::SceneOperation::Reset));
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::StorageError);
  CHECK(service.status() == orbit::SceneServiceStatus::StorageError);
  CHECK(live.applied_bpm == 140);                  // the live scene saved by SaveCurrent is re-applied
  CHECK(scene_bpm(fs, 0) == 140);
  CHECK(live.resumes == 1 && service.failure_count() == 0);
  CHECK(service.active_scene() == 0);
}

// The boot load reads the pointer and the scene without rewriting _activescene. Any write
// would fail here, so a successful load proves there was none.
void load_active_does_not_rewrite_pointer() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 1, 90, true);
  seed_pointer(fs, "2\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  fs.fail_open_write = true;
  CHECK(service.request(orbit::SceneOperation::LoadActive));
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 1 && live.applied_bpm == 90);
  CHECK(pointer_is(fs, "2\n"));
  CHECK(live.resumes == 1);
}

void quiesce_timeout_is_nonblocking() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  LiveState live{};
  live.applied_bpm = 120;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0, 5);
  CHECK(service.request(orbit::SceneOperation::Save));
  service.poll(0); service.poll(1); service.poll(2);
  CHECK(service.phase() == orbit::SceneServicePhase::WaitQuiesce);
  CHECK(fs.storage_calls == 0);
  CHECK(service.poll(101) == orbit::SceneServiceStatus::QuiesceTimeout);
  service.poll(102);
  CHECK(!service.busy() && live.resumes == 1 && fs.storage_calls == 0);
}

void pointer_crlf_ten_loads_scene_ten() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 9, 100);
  seed_pointer(fs, "10\r\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request(orbit::SceneOperation::LoadActive));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 9 && live.applied_bpm == 100);
}

}  // namespace

// Orion.switch_scene saves the current scene, writes the pointer and reloads. The reload finds
// a missing target (load_active_scene: loaded is None), restores the stock defaults and saves
// them to that scene. The service does the same in place, then commits the pointer.
void switch_to_missing_target_applies_defaults() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");                         // Scene02 does not exist
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 1 && pointer_is(fs, "2\n"));
  CHECK(live.applied_bpm == 120);                  // stock defaults in place
  CHECK(scene_bpm(fs, 0) == 140);                  // the outgoing scene was saved first
  CHECK(cell(fs, {orbit::SceneFileKind::Scene, 1}).exists);   // defaults saved to the target
  CHECK(scene_bpm(fs, 1) == 120);
  CHECK(!cell(fs, {orbit::SceneFileKind::SceneTemp, 1}).exists);
  CHECK(!cell(fs, {orbit::SceneFileKind::SceneBackup, 1}).exists);
  CHECK(live.resumes == 1);
}

// The default save to the target fails: the outgoing scene is re-applied in place and the
// pointer does not move. The outgoing scene was saved by SaveCurrent before the failure.
void switch_to_missing_target_save_failure_rolls_back() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  fs.open_write_budget = 1;                        // SaveCurrent writes; the target save cannot open
  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::StorageError);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.applied_bpm == 140);                  // the outgoing scene is back in place
  CHECK(!cell(fs, {orbit::SceneFileKind::Scene, 1}).exists);
  CHECK(scene_bpm(fs, 0) == 140);
  CHECK(live.resumes == 1 && service.failure_count() == 0);
}

// ---- Named scene slots -------------------------------------------------------------------

// CreateScene: current scene saved, defaults in the first free slot, scene file, then names,
// then the pointer. Names and pointer change only after their predecessors are durable.
void create_scene_saves_scene_then_names_then_pointer() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request_create("DELTA"));
  CHECK(service.busy());
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 3 && pointer_is(fs, "4\n"));
  CHECK(service.slot_occupied(3) && std::strcmp(service.slot_name(3), "DELTA") == 0);
  CHECK(service.occupied_count() == 5 && service.active_slot() == 3);
  CHECK(scene_bpm(fs, 0) == 140);  // outgoing live scene was saved first
  CHECK(scene_bpm(fs, 3) == 120);  // new scene holds the stock defaults
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0},
                "01,ALFA\n02,BRAVO\n03,CHARLIE\n04,DELTA\n10,JULIET\n"));
  CHECK(live.applied_bpm == 120);
  CHECK(live.resumes == 1);
}

// Refusals are reported through status() and never start an operation.
void create_scene_refusals_have_distinct_status() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(!service.request_create("bravo") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_create("") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_create("ABCDEFGHI") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_create("A-B") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_create("ALFA") && service.status() == orbit::SceneServiceStatus::NameTaken);
  CHECK(!service.busy());
  CHECK(service.occupied_count() == 4);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames));
}

void create_scene_without_free_slot_is_refused() {
  static MemoryFs fs{};
  reset_fs(fs);
  char text[16 * 8 + 1]{};
  std::size_t used = 0;
  for (int slot = 1; slot <= 16; ++slot) used += static_cast<std::size_t>(std::snprintf(text + used, sizeof(text) - used, "%02d,N%02d\n", slot, slot));
  seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, text);
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.occupied_count() == 16);
  CHECK(!service.request_create("ZULU") && service.status() == orbit::SceneServiceStatus::NoFreeSlot);
  CHECK(!service.busy());
}

// Switch, Load and CopyCurrent to an unnamed slot are refused; the active slot stays named.
void unoccupied_slots_are_refused_for_slot_operations() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(!service.request(orbit::SceneOperation::Switch, 4) &&
        service.status() == orbit::SceneServiceStatus::SlotUnoccupied);
  CHECK(!service.request(orbit::SceneOperation::Load, 4) &&
        service.status() == orbit::SceneServiceStatus::SlotUnoccupied);
  CHECK(!service.request(orbit::SceneOperation::CopyCurrent, 4) &&
        service.status() == orbit::SceneServiceStatus::SlotUnoccupied);
  CHECK(!service.request(orbit::SceneOperation::CreateScene, 4));  // only request_create creates
  CHECK(!service.busy() && service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(service.request(orbit::SceneOperation::Switch, 1));  // BRAVO is named
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 1 && pointer_is(fs, "2\n"));
}

// First run: no names file -> "ALFA" in slot 0 from the defaults, one boot-time write set.
void first_run_creates_alfa_in_slot_zero() {
  static MemoryFs fs{};
  reset_fs(fs);
  cell(fs, {orbit::SceneFileKind::SceneNames, 0}).exists = false;
  cell(fs, {orbit::SceneFileKind::SceneNames, 0}).size = 0;
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(service.request(orbit::SceneOperation::LoadNames));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.occupied_count() == 1 && service.slot_occupied(0));
  CHECK(std::strcmp(service.slot_name(0), "ALFA") == 0);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, "01,ALFA\n"));
  CHECK(scene_bpm(fs, 0) == 120);
  CHECK(!cell(fs, {orbit::SceneFileKind::SceneNamesTemp, 0}).exists);
  CHECK(service.request(orbit::SceneOperation::LoadActive));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(live.applied_bpm == 120);
}

// Malformed lines are skipped and counted; a readable file is never rewritten at boot.
void malformed_name_lines_are_skipped_and_counted() {
  static MemoryFs fs{};
  reset_fs(fs);
  seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, "01,ALFA\nxx,bad\n02,bravo\n02,BRAVO\n\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.occupied_count() == 2 && service.slot_occupied(0) && service.slot_occupied(1));
  CHECK(service.scene_names().malformed_lines == 2);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, "01,ALFA\nxx,bad\n02,bravo\n02,BRAVO\n\n"));
  CHECK(!cell(fs, {orbit::SceneFileKind::ActivePointer, 0}).exists);
  CHECK(!cell(fs, {orbit::SceneFileKind::Scene, 0}).exists);
}

// Unreadable names (no valid line, or oversize) are never replaced by a boot-time ALFA.
void unreadable_names_never_auto_create() {
  {
    static MemoryFs fs{};
    reset_fs(fs);
    seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, "xx,bad\n");
    LiveState live{};
    orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
    CHECK(service.request(orbit::SceneOperation::LoadNames));
    CHECK(complete(service, live) == orbit::SceneServiceStatus::SceneError);
    CHECK(service.occupied_count() == 0);
    CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, "xx,bad\n"));
    CHECK(!cell(fs, {orbit::SceneFileKind::Scene, 0}).exists);
    CHECK(!cell(fs, {orbit::SceneFileKind::ActivePointer, 0}).exists);
  }
  {
    static MemoryFs fs{};
    reset_fs(fs);
    char text[321]{};  // 320 bytes plus the terminator seed_text measures
    for (std::size_t i = 0; i + 8 <= 320; i += 8) std::memcpy(text + i, "01,ALFA\n", 8);
    seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, text);
    LiveState live{};
    orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
    CHECK(service.request(orbit::SceneOperation::LoadNames));
    CHECK(complete(service, live) == orbit::SceneServiceStatus::StorageError);
    CHECK(service.occupied_count() == 0);
    CHECK(!cell(fs, {orbit::SceneFileKind::Scene, 0}).exists);
  }
}

// A pointer at an unnamed slot falls back to the first named slot.
void stale_pointer_to_unnamed_slot_falls_back_to_first_named() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "5\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request(orbit::SceneOperation::LoadActive));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 0 && live.applied_bpm == 120);
}

// A failed pointer commit after the names write: the names file is restored to the old set,
// the scene is rolled back in place, and the in-memory names match storage again.
void create_pointer_failure_restores_names_and_scene() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  live.applied_bpm = 140;
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  fs.fail_pointer_install = true;
  CHECK(service.request_create("DELTA"));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::StorageError);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(!service.slot_occupied(3) && service.occupied_count() == 4);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames));
  CHECK(live.applied_bpm == 140);  // the outgoing scene is back in place
  CHECK(scene_bpm(fs, 0) == 140);
  CHECK(live.resumes == 1 && service.failure_count() == 0);
}

// A failed names write leaves no name; the orphan scene file is invisible and harmless.
void create_names_write_failure_leaves_no_name() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  // SaveCurrent and the new scene file each open a temp; the names temp cannot open.
  fs.open_write_budget = 2;
  CHECK(service.request_create("DELTA"));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::StorageError);
  CHECK(!service.slot_occupied(3) && service.occupied_count() == 4);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames));
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.resumes == 1);
}

// Unreadable names are not trusted: Create and slot operations are refused and neither the
// scene file nor the names file is touched, so the unreadable names cannot be overwritten.
void corrupt_names_block_create_and_slot_operations() {
  static MemoryFs fs{};
  static Cell scene_before{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  scene_before = cell(fs, {orbit::SceneFileKind::Scene, 0});
  seed_text(fs, {orbit::SceneFileKind::SceneNames, 0}, "xx,bad\n");
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(service.request(orbit::SceneOperation::LoadNames));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::SceneError);
  CHECK(!service.names_loaded() && service.occupied_count() == 0);
  CHECK(!service.request_create("DELTA") && service.status() == orbit::SceneServiceStatus::NamesUnavailable);
  CHECK(!service.request(orbit::SceneOperation::Switch, 0) &&
        service.status() == orbit::SceneServiceStatus::NamesUnavailable);
  CHECK(!service.busy());
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, "xx,bad\n"));
  const auto& scene_now = cell(fs, {orbit::SceneFileKind::Scene, 0});
  CHECK(scene_now.size == scene_before.size &&
        std::memcmp(scene_now.bytes, scene_before.bytes, scene_before.size) == 0);
  CHECK(pointer_is(fs, "1\n"));
}

// invalidate_names (a committed CDC names change or a format) stops create and slot
// operations until the next boot. Save of the active scene keeps working.
void invalidate_names_blocks_slot_operations_until_boot() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.names_loaded());
  service.invalidate_names();
  CHECK(!service.names_loaded());
  CHECK(!service.request_create("DELTA") && service.status() == orbit::SceneServiceStatus::NamesUnavailable);
  CHECK(!service.request(orbit::SceneOperation::Switch, 1) &&
        service.status() == orbit::SceneServiceStatus::NamesUnavailable);
  CHECK(service.request(orbit::SceneOperation::Save));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
}

// Rename of the active slot: only the names file changes (scene file and pointer untouched).
void rename_scene_changes_only_the_names_file() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(service.request_rename("ZULU"));
  CHECK(service.busy());
  CHECK(complete(service, live) == orbit::SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(service.slot_name(0), "ZULU") == 0);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, "01,ZULU\n02,BRAVO\n03,CHARLIE\n10,JULIET\n"));
  CHECK(scene_bpm(fs, 0) == 120 && pointer_is(fs, "1\n"));
  CHECK(service.active_scene() == 0 && live.resumes == 1);
  // The slot keeps its own name: accepted as a rewrite of the same text.
  CHECK(service.request_rename("ZULU") && complete(service, live) == orbit::SceneServiceStatus::Succeeded);
}

// Refusals report distinct statuses and change nothing.
void rename_refusals_have_distinct_status() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(!service.request_rename("ZULU") && service.status() == orbit::SceneServiceStatus::NamesUnavailable);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  CHECK(!service.request_rename("zulu") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_rename("") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_rename("ABCDEFGHI") && service.status() == orbit::SceneServiceStatus::NameInvalid);
  CHECK(!service.request_rename("BRAVO") && service.status() == orbit::SceneServiceStatus::NameTaken);
  CHECK(!service.busy());
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames));
  CHECK(std::strcmp(service.slot_name(0), "ALFA") == 0);
}

// A failed names write keeps the old name in memory and leaves the file as it was.
void rename_names_write_failure_keeps_old_name() {
  static MemoryFs fs{};
  reset_fs(fs);
  put_scene(fs, 0, 120);
  seed_pointer(fs, "1\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
  CHECK(load_names(service, live, fs) == orbit::SceneServiceStatus::Succeeded);
  fs.open_write_budget = 0;  // the names temp cannot open
  CHECK(service.request_rename("ZULU"));
  CHECK(complete(service, live) == orbit::SceneServiceStatus::StorageError);
  CHECK(std::strcmp(service.slot_name(0), "ALFA") == 0);
  CHECK(text_is(fs, {orbit::SceneFileKind::SceneNames, 0}, kDefaultNames));
  CHECK(pointer_is(fs, "1\n") && service.active_scene() == 0);
}

int main() {
  rename_scene_changes_only_the_names_file();
  rename_refusals_have_distinct_status();
  rename_names_write_failure_keeps_old_name();
  corrupt_names_block_create_and_slot_operations();
  invalidate_names_blocks_slot_operations_until_boot();
  create_scene_saves_scene_then_names_then_pointer();
  create_scene_refusals_have_distinct_status();
  create_scene_without_free_slot_is_refused();
  unoccupied_slots_are_refused_for_slot_operations();
  first_run_creates_alfa_in_slot_zero();
  malformed_name_lines_are_skipped_and_counted();
  unreadable_names_never_auto_create();
  stale_pointer_to_unnamed_slot_falls_back_to_first_named();
  create_pointer_failure_restores_names_and_scene();
  create_names_write_failure_leaves_no_name();
  switch_and_copy_transaction();
  file_and_pointer_failures_restore();
  recovery_pointer_and_reset();
  quiesce_timeout_is_nonblocking();
  recovery_after_rollback_failure();
  recovery_read_failure_stays_failed();
  pointer_crlf_ten_loads_scene_ten();
  reset_save_failure_reports_storage_error();
  load_active_does_not_rewrite_pointer();
  switch_to_missing_target_applies_defaults();
  switch_to_missing_target_save_failure_rolls_back();
  if (failures) return 1;
  std::puts("T07b scene service checks passed");
  return 0;
}
