#include <cstdint>
#include <cstdio>
#include <cstring>

#include "persistence/scene_service.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

constexpr std::size_t kKinds = 6;
constexpr std::size_t kScenes = 10;
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
  seed_pointer(fs, "4\r\n");
  LiveState live{};
  orbit::SceneService service(storage_port(fs), lifecycle_port(live), kSchema, 0);
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
  fs.open_write_budget = 1;                        // SaveCurrent writes; the target save cannot open
  CHECK(service.request(orbit::SceneOperation::Switch, 1));
  CHECK(complete(service, live, 0) == orbit::SceneServiceStatus::StorageError);
  CHECK(service.active_scene() == 0 && pointer_is(fs, "1\n"));
  CHECK(live.applied_bpm == 140);                  // the outgoing scene is back in place
  CHECK(!cell(fs, {orbit::SceneFileKind::Scene, 1}).exists);
  CHECK(scene_bpm(fs, 0) == 140);
  CHECK(live.resumes == 1 && service.failure_count() == 0);
}

int main() {
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
