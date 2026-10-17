#include "scene_service.hpp"

#include <cstring>

#include "scene_names.hpp"

namespace orbit {
namespace {

SceneFileId scene_file(SceneFileKind kind, std::uint8_t scene) noexcept {
  return {kind, scene};
}

SceneServiceStatus map_codec(SceneResult result) noexcept {
  return result == SceneResult::Ok ? SceneServiceStatus::Succeeded :
         (result == SceneResult::Missing ? SceneServiceStatus::SceneError :
                                          SceneServiceStatus::SceneError);
}

void clear_scene_dto(SceneDto& dto) noexcept {
  dto.valid = false;
  dto.banks.present = 0;
  for (auto& meta : dto.banks.meta) meta = SceneInputBankMeta{};
  dto.global_fields = 0;
  dto.diagnostics = SceneDiagnosticNone;
  dto.globals = GlobalState{};
  for (std::uint8_t slot = 0; slot < kSceneModuleSlots; ++slot) {
    auto& module = dto.modules[slot];
    module.present = false;
    module.unknown_module = false;
    module.id[0] = '\0';
    module.version = 1;
    module.parameter_count = 0;
    module.attribute_count = 0;
  }
}

}  // namespace

SceneService::SceneService(const SceneStoragePort& storage,
                           const SceneLifecyclePort& lifecycle,
                           const SceneSchemaResolver& schema,
                           std::uint8_t active_scene,
                           std::uint32_t quiesce_timeout_ms) noexcept
    : storage_(storage), lifecycle_(lifecycle), schema_(schema),
      active_scene_(active_scene < kSceneSlotCount ? active_scene : 0),
      quiesce_timeout_ms_(quiesce_timeout_ms <= 0x7fffffffU ? quiesce_timeout_ms : 0x7fffffffU) {}

bool SceneService::valid_ports() const noexcept {
  return storage_.exists != nullptr && storage_.open_read != nullptr &&
         storage_.open_write != nullptr && storage_.close != nullptr &&
         storage_.rename != nullptr && storage_.remove != nullptr &&
         lifecycle_.is_stopped != nullptr && lifecycle_.stop_release_drain != nullptr &&
         lifecycle_.request_quiesce != nullptr && lifecycle_.quiesced != nullptr &&
         lifecycle_.quiesce_timed_out != nullptr && lifecycle_.cancel_quiesce != nullptr &&
         lifecycle_.resume != nullptr && lifecycle_.capture != nullptr &&
         lifecycle_.prepare_scene != nullptr && lifecycle_.apply_globals != nullptr &&
         lifecycle_.construct_module != nullptr && lifecycle_.apply_parameter != nullptr &&
         lifecycle_.apply_attribute != nullptr && lifecycle_.finish_scene != nullptr &&
         lifecycle_.rebuild_ui != nullptr;
}

void SceneService::begin(SceneOperation operation, std::uint8_t target_scene) noexcept {
  operation_ = operation;
  target_scene_ = target_scene;
  previous_scene_ = active_scene_;
  backup_retained_ = false;
  quiesce_requested_ = false;
  // CreateScene rolls back only when a valid live scene exists to return to (first run has none).
  rollback_needed_ = operation_ == SceneOperation::Switch || operation_ == SceneOperation::Load ||
                     operation_ == SceneOperation::Reset ||
                     (operation_ == SceneOperation::CreateScene && names_.occupied(active_scene_));
  pointer_commit_ = false;
  target_missing_ = false;
  codec_result_ = SceneResult::Ok;
  status_ = SceneServiceStatus::Busy;
  phase_ = SceneServicePhase::Stop;
}

bool SceneService::request(SceneOperation operation, std::uint8_t target_scene) noexcept {
  if (busy()) return false;
  const bool settings_op = operation == SceneOperation::SaveUserSettings ||
                           operation == SceneOperation::LoadUserSettings;
  if (!valid_ports() || operation == SceneOperation::CreateScene ||
      (operation != SceneOperation::Reset && operation != SceneOperation::Save &&
       !settings_op && target_scene >= kSceneSlotCount)) {
    status_ = SceneServiceStatus::Rejected;
    return false;
  }
  const bool slot_operation = operation == SceneOperation::Switch ||
                              operation == SceneOperation::Load ||
                              operation == SceneOperation::CopyCurrent;
  if (slot_operation && !names_loaded_) {
    status_ = SceneServiceStatus::NamesUnavailable;
    return false;
  }
  if (slot_operation && !names_.occupied(target_scene)) {
    status_ = SceneServiceStatus::SlotUnoccupied;
    return false;
  }
  if (operation == SceneOperation::Switch && target_scene == active_scene_)
    operation = SceneOperation::Save;
  begin(operation, target_scene);
  return true;
}

bool SceneService::request_create(const char* name) noexcept {
  if (busy()) return false;
  if (!valid_ports()) {
    status_ = SceneServiceStatus::Rejected;
    return false;
  }
  // Never create over slot files whose names were not read: an unreadable names file must
  // not be replaced, and an unnamed slot may hold a real scene.
  if (!names_loaded_) {
    status_ = SceneServiceStatus::NamesUnavailable;
    return false;
  }
  if (!name_valid(name)) {
    status_ = SceneServiceStatus::NameInvalid;
    return false;
  }
  if (name_taken(names_, name)) {
    status_ = SceneServiceStatus::NameTaken;
    return false;
  }
  std::uint8_t slot = 0;
  if (!first_free_slot(names_, slot)) {
    status_ = SceneServiceStatus::NoFreeSlot;
    return false;
  }
  // name_valid bounds the text to kSceneNameMaxChars, so the copy fits pending_name_.
  std::memcpy(pending_name_, name, std::strlen(name) + 1);
  begin(SceneOperation::CreateScene, slot);
  return true;
}

bool SceneService::request_rename(const char* name) noexcept {
  if (busy()) return false;
  if (!valid_ports()) {
    status_ = SceneServiceStatus::Rejected;
    return false;
  }
  if (!names_loaded_) {
    status_ = SceneServiceStatus::NamesUnavailable;
    return false;
  }
  if (!names_.occupied(active_scene_)) {
    status_ = SceneServiceStatus::SlotUnoccupied;
    return false;
  }
  if (!name_valid(name)) {
    status_ = SceneServiceStatus::NameInvalid;
    return false;
  }
  // The active slot may keep its own name (a no-op rename); another saved scene may not hold it.
  for (std::size_t slot = 0; slot < kSceneSlotCount; ++slot) {
    if (slot == active_scene_ || !names_.occupied(static_cast<std::uint8_t>(slot))) continue;
    if (std::strcmp(names_.names[slot], name) == 0) {
      status_ = SceneServiceStatus::NameTaken;
      return false;
    }
  }
  std::memcpy(pending_name_, name, std::strlen(name) + 1);  // name_valid bounds the length
  begin(SceneOperation::RenameScene, active_scene_);
  return true;
}

SceneServiceStatus SceneService::rename_slot(std::uint8_t slot, const char* name) noexcept {
  if (busy()) return SceneServiceStatus::Busy;
  if (!valid_ports()) return SceneServiceStatus::Rejected;
  if (!names_loaded_) return SceneServiceStatus::NamesUnavailable;
  if (slot >= kSceneSlotCount || !names_.occupied(slot)) return SceneServiceStatus::SlotUnoccupied;
  if (!name_valid(name)) return SceneServiceStatus::NameInvalid;
  if (std::strcmp(names_.names[slot], name) == 0) return SceneServiceStatus::Succeeded;  // nothing to write
  for (std::size_t other = 0; other < kSceneSlotCount; ++other) {
    if (other == slot || !names_.occupied(other)) continue;
    if (std::strcmp(names_.names[other], name) == 0) return SceneServiceStatus::NameTaken;
  }
  SceneNames candidate = names_;
  candidate.set(slot, name);
  const SceneServiceStatus status = write_names_atomic(candidate);
  if (status == SceneServiceStatus::Succeeded || status == SceneServiceStatus::SucceededWithBackup) {
    if (status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
    names_ = candidate;  // memory changes only after the names file verified
    return SceneServiceStatus::Succeeded;
  }
  // RecoveryRequired leaves the names file in an unknown state: stop trusting the set.
  if (status == SceneServiceStatus::RecoveryRequired) names_loaded_ = false;
  return status;
}

bool SceneService::file_exists(SceneFileId id, bool& exists) noexcept {
  const auto status = storage_.exists(storage_.context, id, exists);
  if (status == SceneStorageStatus::Missing) { exists = false; return true; }
  return status == SceneStorageStatus::Ok;
}

bool SceneService::remove_if_present(SceneFileId id) noexcept {
  const auto status = storage_.remove(storage_.context, id);
  return status == SceneStorageStatus::Ok || status == SceneStorageStatus::Missing;
}

SceneServiceStatus SceneService::recover_file(SceneFileId target, SceneFileId temp,
                                               SceneFileId backup) noexcept {
  bool target_exists = false, temp_exists = false, backup_exists = false;
  if (!file_exists(target, target_exists) || !file_exists(temp, temp_exists) ||
      !file_exists(backup, backup_exists)) return SceneServiceStatus::StorageError;
  if (!target_exists && backup_exists) {
    if (storage_.rename(storage_.context, backup, target) != SceneStorageStatus::Ok)
      return SceneServiceStatus::RecoveryRequired;
    target_exists = true;
    backup_exists = false;
  } else if (target_exists && backup_exists) {
    // Target rename completed before power loss; the backup is a retained old version.
    if (!remove_if_present(backup)) backup_retained_ = true;
    else backup_exists = false;
  }
  if (temp_exists && !remove_if_present(temp)) return SceneServiceStatus::StorageError;
  (void)target_exists;
  (void)backup_exists;
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::capture() noexcept {
  clear_scene_dto(staging_);
  if (!lifecycle_.capture(lifecycle_.context, staging_)) return SceneServiceStatus::SceneError;
  staging_.valid = true;
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::read_scene(std::uint8_t scene) noexcept {
  const auto recovered = recover_file(scene_file(SceneFileKind::Scene, scene),
                                      scene_file(SceneFileKind::SceneTemp, scene),
                                      scene_file(SceneFileKind::SceneBackup, scene));
  if (recovered != SceneServiceStatus::Succeeded) return recovered;
  SceneReader reader{};
  const auto opened = storage_.open_read(storage_.context,
                                         scene_file(SceneFileKind::Scene, scene), reader);
  if (opened == SceneStorageStatus::Missing) {
    codec_result_ = SceneResult::Missing;
    target_missing_ = true;
    return SceneServiceStatus::SceneError;
  }
  if (opened != SceneStorageStatus::Ok) return SceneServiceStatus::StorageError;
  codec_result_ = decode_scene(reader, schema_, staging_);
  const auto closed = storage_.close(storage_.context, scene_file(SceneFileKind::Scene, scene));
  if (closed != SceneStorageStatus::Ok) return SceneServiceStatus::StorageError;
  return map_codec(codec_result_);
}

SceneServiceStatus SceneService::write_scene_atomic(std::uint8_t scene) noexcept {
  const auto target = scene_file(SceneFileKind::Scene, scene);
  const auto temp = scene_file(SceneFileKind::SceneTemp, scene);
  const auto backup = scene_file(SceneFileKind::SceneBackup, scene);
  auto result = recover_file(target, temp, backup);
  if (result != SceneServiceStatus::Succeeded) return result;
  if (!remove_if_present(temp)) return SceneServiceStatus::StorageError;

  SceneWriter writer{};
  if (storage_.open_write(storage_.context, temp, writer) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  std::size_t written = 0;
  codec_result_ = encode_scene(staging_, schema_, writer, written);
  const auto write_close = storage_.close(storage_.context, temp);
  if (codec_result_ != SceneResult::Ok || write_close != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return codec_result_ == SceneResult::Ok ? SceneServiceStatus::StorageError : SceneServiceStatus::SceneError;
  }

  SceneReader verify_reader{};
  if (storage_.open_read(storage_.context, temp, verify_reader) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  codec_result_ = decode_scene(verify_reader, schema_, staging_);
  const auto verify_close = storage_.close(storage_.context, temp);
  if (codec_result_ != SceneResult::Ok || verify_close != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return codec_result_ == SceneResult::Ok ? SceneServiceStatus::StorageError : SceneServiceStatus::SceneError;
  }

  bool target_exists = false;
  if (!file_exists(target, target_exists)) return SceneServiceStatus::StorageError;
  if (target_exists && storage_.rename(storage_.context, target, backup) != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (storage_.rename(storage_.context, temp, target) != SceneStorageStatus::Ok) {
    if (target_exists && storage_.rename(storage_.context, backup, target) != SceneStorageStatus::Ok)
      return SceneServiceStatus::RecoveryRequired;
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (target_exists && !remove_if_present(backup)) {
    backup_retained_ = true;
    return SceneServiceStatus::SucceededWithBackup;
  }
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::read_pointer(std::uint8_t& scene) noexcept {
  const auto recovered = recover_file(scene_file(SceneFileKind::ActivePointer, 0),
                                      scene_file(SceneFileKind::ActiveTemp, 0),
                                      scene_file(SceneFileKind::ActiveBackup, 0));
  if (recovered != SceneServiceStatus::Succeeded) return recovered;
  SceneReader reader{};
  const auto opened = storage_.open_read(storage_.context,
                                         scene_file(SceneFileKind::ActivePointer, 0), reader);
  if (opened == SceneStorageStatus::Missing) { scene = 0; codec_result_ = SceneResult::Missing; return SceneServiceStatus::Succeeded; }
  if (opened != SceneStorageStatus::Ok) return SceneServiceStatus::StorageError;
  // Five bytes hold the longest valid CRLF pointer ("10\r\n") plus the End probe.
  char text[5]{};
  std::size_t count = 0;
  SceneReadStatus read_status = SceneReadStatus::Byte;
  while (count < sizeof(text) && (read_status = reader.read(reader.context, text[count])) == SceneReadStatus::Byte)
    ++count;
  if (read_status == SceneReadStatus::Error || read_status == SceneReadStatus::Missing) {
    storage_.close(storage_.context, scene_file(SceneFileKind::ActivePointer, 0));
    return SceneServiceStatus::StorageError;
  }
  const auto closed = storage_.close(storage_.context, scene_file(SceneFileKind::ActivePointer, 0));
  if (closed != SceneStorageStatus::Ok) return SceneServiceStatus::StorageError;
  if (read_status != SceneReadStatus::End) return SceneServiceStatus::SceneError;
  if (!parse_active_pointer(text, count, scene)) return SceneServiceStatus::SceneError;
  codec_result_ = SceneResult::Ok;
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::write_pointer_atomic(std::uint8_t scene) noexcept {
  const auto target = scene_file(SceneFileKind::ActivePointer, 0);
  const auto temp = scene_file(SceneFileKind::ActiveTemp, 0);
  const auto backup = scene_file(SceneFileKind::ActiveBackup, 0);
  const auto recovered = recover_file(target, temp, backup);
  if (recovered != SceneServiceStatus::Succeeded || !remove_if_present(temp))
    return recovered == SceneServiceStatus::Succeeded ? SceneServiceStatus::StorageError : recovered;
  char expected[4]{};
  const std::size_t expected_size = format_active_pointer(scene, expected, sizeof(expected));
  if (expected_size == 0) return SceneServiceStatus::SceneError;
  SceneWriter writer{};
  if (storage_.open_write(storage_.context, temp, writer) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  if (writer.write == nullptr ||
      writer.write(writer.context, expected, expected_size) != SceneWriteStatus::Written) {
    storage_.close(storage_.context, temp);
    return SceneServiceStatus::StorageError;
  }
  const auto close_status = storage_.close(storage_.context, temp);
  if (close_status != SceneStorageStatus::Ok) return SceneServiceStatus::StorageError;

  SceneReader verify{};
  if (storage_.open_read(storage_.context, temp, verify) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  char actual[4]{};
  std::uint8_t actual_size = 0;
  char byte = 0;
  SceneReadStatus rs = SceneReadStatus::Byte;
  while (actual_size < sizeof(actual) && (rs = verify.read(verify.context, byte)) == SceneReadStatus::Byte)
    actual[actual_size++] = byte;
  const auto verify_close = storage_.close(storage_.context, temp);
  if (rs != SceneReadStatus::End || verify_close != SceneStorageStatus::Ok ||
      static_cast<std::size_t>(actual_size) != expected_size ||
      std::memcmp(actual, expected, expected_size) != 0) {
    remove_if_present(temp);
    return rs == SceneReadStatus::Error ? SceneServiceStatus::StorageError : SceneServiceStatus::SceneError;
  }
  bool target_exists = false;
  if (!file_exists(target, target_exists)) return SceneServiceStatus::StorageError;
  if (target_exists && storage_.rename(storage_.context, target, backup) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  if (storage_.rename(storage_.context, temp, target) != SceneStorageStatus::Ok) {
    if (target_exists && storage_.rename(storage_.context, backup, target) != SceneStorageStatus::Ok)
      return SceneServiceStatus::RecoveryRequired;
    return SceneServiceStatus::StorageError;
  }
  if (target_exists && !remove_if_present(backup)) {
    backup_retained_ = true;
    return SceneServiceStatus::SucceededWithBackup;
  }
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::apply_staging() noexcept {
  if (!staging_.valid || !lifecycle_.prepare_scene(lifecycle_.context) ||
      !lifecycle_.apply_globals(lifecycle_.context, staging_.globals, staging_.global_fields))
    return SceneServiceStatus::ApplyError;
  for (std::uint8_t slot = 0; slot < kSceneModuleSlots; ++slot) {
    const auto& module = staging_.modules[slot];
    if (!module.present) continue;
    if (!lifecycle_.construct_module(lifecycle_.context, slot, module.id, module.version,
                                     module.unknown_module)) return SceneServiceStatus::ApplyError;
    for (std::uint8_t i = 0; i < module.parameter_count; ++i)
      if (!lifecycle_.apply_parameter(lifecycle_.context, slot, module.parameters[i]))
        return SceneServiceStatus::ApplyError;
    for (std::uint8_t i = 0; i < module.attribute_count; ++i)
      if (!lifecycle_.apply_attribute(lifecycle_.context, slot, module.attributes[i]))
        return SceneServiceStatus::ApplyError;
  }
  if (lifecycle_.apply_banks != nullptr && !lifecycle_.apply_banks(lifecycle_.context, staging_))
    return SceneServiceStatus::ApplyError;
  return lifecycle_.finish_scene(lifecycle_.context) ? SceneServiceStatus::Succeeded :
                                                        SceneServiceStatus::ApplyError;
}

void SceneService::fail(SceneServiceStatus status) noexcept {
  status_ = status;
  phase_ = SceneServicePhase::Resume;
}

void SceneService::finish_resume(bool success) noexcept {
  if (success && status_ == SceneServiceStatus::Busy)
    status_ = backup_retained_ ? SceneServiceStatus::SucceededWithBackup : SceneServiceStatus::Succeeded;
  if (quiesce_requested_) lifecycle_.resume(lifecycle_.context, quiesce_sequence_);
  quiesce_requested_ = false;
  phase_ = SceneServicePhase::Idle;
}

void SceneService::fail_or_rollback(SceneServiceStatus status) noexcept {
  // The operation's own status is kept (never replaced by the rollback result),
  // so a failed Reset/save reports the storage error that caused it.
  status_ = status;
  if (rollback_needed_) phase_ = SceneServicePhase::RollbackRead;
  else fail(status);
}

void SceneService::enter_failed() noexcept {
  // Failed stays busy but releases the quiesced window: the core-1 worker resumes
  // so it can scan the recovery gesture, and no storage write happens until
  // acknowledge_recovery() requests a new quiesce.
  status_ = SceneServiceStatus::RecoveryRequired;
  if (quiesce_requested_) lifecycle_.resume(lifecycle_.context, quiesce_sequence_);
  quiesce_requested_ = false;
  phase_ = SceneServicePhase::Failed;
  ++failure_count_;
}

SceneServiceStatus SceneService::poll(std::uint32_t now_ms) noexcept {
  if (!busy()) return status_;
  SceneServiceStatus operation_status = SceneServiceStatus::Busy;
  switch (phase_) {
    case SceneServicePhase::Stop:
      lifecycle_.stop_release_drain(lifecycle_.context);
      phase_ = SceneServicePhase::WaitStopped;
      break;
    case SceneServicePhase::WaitStopped:
      if (lifecycle_.is_stopped(lifecycle_.context)) {
        quiesce_sequence_ = lifecycle_.request_quiesce(lifecycle_.context, now_ms,
                                                       quiesce_timeout_ms_);
        if (quiesce_sequence_ == 0) { fail(SceneServiceStatus::QuiesceTimeout); break; }
        quiesce_requested_ = true;
        phase_ = SceneServicePhase::WaitQuiesce;
      }
      break;
    case SceneServicePhase::WaitQuiesce:
      if (lifecycle_.quiesce_timed_out(lifecycle_.context, quiesce_sequence_, now_ms)) {
        lifecycle_.cancel_quiesce(lifecycle_.context, quiesce_sequence_);
        fail(SceneServiceStatus::QuiesceTimeout);
      } else if (lifecycle_.quiesced(lifecycle_.context, quiesce_sequence_)) {
        phase_ = operation_ == SceneOperation::RenameScene ? SceneServicePhase::WriteNames :
                 operation_ == SceneOperation::LoadActive ? SceneServicePhase::ReadActive :
                 operation_ == SceneOperation::Load ? SceneServicePhase::ReadTarget :
                 operation_ == SceneOperation::SaveUserSettings ? SceneServicePhase::WriteUserSettings :
                 operation_ == SceneOperation::LoadUserSettings ? SceneServicePhase::ReadUserSettings :
                 operation_ == SceneOperation::LoadNames ? SceneServicePhase::ReadNames :
                 // Create: an unnamed active slot (no names yet) has nothing to save.
                 operation_ == SceneOperation::CreateScene && !names_.occupied(active_scene_) ?
                     SceneServicePhase::ResetDefaults :
                                                       SceneServicePhase::CaptureCurrent;
      }
      break;
    case SceneServicePhase::ReadActive:
      operation_status = read_pointer(target_scene_);
      if (operation_status != SceneServiceStatus::Succeeded) {
        fail(operation_status);
      } else {
        // The pointer's index is kept even when its scene fails to load (scenes.py /
        // Orion.load_active_scene): a later save then targets that same scene, never another.
        // A pointer at an unoccupied slot (stale or hand-edited) falls back to the first
        // named slot. Only applies once names are loaded: with no names, the pointer stands.
        std::uint8_t first = 0;
        if (names_.occupied_count() > 0 && !names_.occupied(target_scene_) &&
            first_occupied_slot(names_, first))
          target_scene_ = first;
        active_scene_ = target_scene_;
        phase_ = SceneServicePhase::ReadTarget;
      }
      break;
    case SceneServicePhase::CaptureCurrent:
      operation_status = capture();
      if (operation_status != SceneServiceStatus::Succeeded) fail(operation_status);
      else phase_ = SceneServicePhase::SaveCurrent;
      break;
    case SceneServicePhase::SaveCurrent:
      operation_status = write_scene_atomic(operation_ == SceneOperation::CopyCurrent ? target_scene_ : active_scene_);
      if (operation_status != SceneServiceStatus::Succeeded &&
          operation_status != SceneServiceStatus::SucceededWithBackup) { fail(operation_status); break; }
      if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
      if (operation_ == SceneOperation::Save) finish_resume(true);
      else if (operation_ == SceneOperation::Switch) phase_ = SceneServicePhase::ReadTarget;
      else if (operation_ == SceneOperation::CopyCurrent) phase_ = SceneServicePhase::CommitPointer;
      else phase_ = SceneServicePhase::ResetDefaults;  // Reset, CreateScene
      break;
    case SceneServicePhase::ReadTarget:
      operation_status = read_scene(target_scene_);
      if (operation_status == SceneServiceStatus::SceneError && target_missing_ &&
          operation_ == SceneOperation::Switch) {
        // Missing target (Orion.load_active_scene, loaded is None): stock defaults in place,
        // saved to the target file, then the pointer moves. Nothing is written before the
        // current scene was saved by SaveCurrent, so a failure rolls back to it.
        phase_ = SceneServicePhase::ResetDefaults;
      } else if (operation_status != SceneServiceStatus::Succeeded) {
        fail(operation_status);
      } else {
        phase_ = SceneServicePhase::Apply;
      }
      break;
    case SceneServicePhase::ResetDefaults:
      if (lifecycle_.reset_defaults == nullptr || !lifecycle_.reset_defaults(lifecycle_.context)) {
        fail_or_rollback(SceneServiceStatus::ApplyError);
      } else phase_ = SceneServicePhase::CaptureReset;
      break;
    case SceneServicePhase::CaptureReset:
      operation_status = capture();
      if (operation_status != SceneServiceStatus::Succeeded) fail_or_rollback(operation_status);
      else phase_ = SceneServicePhase::SaveReset;
      break;
    case SceneServicePhase::SaveReset:
      // Reset saves the active scene; a Switch to a missing target saves the target.
      // Create saves the new scene file first; its name is written only after this succeeds.
      operation_status = write_scene_atomic(operation_ == SceneOperation::Switch ||
                                                    operation_ == SceneOperation::CreateScene
                                                ? target_scene_ : active_scene_);
      if (operation_status == SceneServiceStatus::Succeeded ||
          operation_status == SceneServiceStatus::SucceededWithBackup) {
        if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
        phase_ = operation_ == SceneOperation::CreateScene ? SceneServicePhase::WriteNames
                 : operation_ == SceneOperation::Switch ? SceneServicePhase::CommitPointer
                                                        : SceneServicePhase::Rebuild;
      } else {
        fail_or_rollback(operation_status);
      }
      break;
    case SceneServicePhase::Apply:
      operation_status = apply_staging();
      if (operation_status != SceneServiceStatus::Succeeded) fail_or_rollback(operation_status);
      else if (operation_ == SceneOperation::CopyCurrent) phase_ = SceneServicePhase::Rebuild;
      else if (operation_ == SceneOperation::LoadActive) {
        // Boot load: the pointer already names this scene, so it is not rewritten.
        active_scene_ = target_scene_;
        phase_ = SceneServicePhase::Rebuild;
      } else phase_ = SceneServicePhase::CommitPointer;
      break;
    case SceneServicePhase::WriteNames: {
      // Candidate is the in-memory set plus the new name; memory changes only after the
      // names file verifies, so memory always matches what was last committed to storage.
      SceneNames candidate = names_;
      candidate.set(target_scene_, pending_name_);
      operation_status = write_names_atomic(candidate);
      if (operation_status != SceneServiceStatus::Succeeded &&
          operation_status != SceneServiceStatus::SucceededWithBackup) {
        // RecoveryRequired leaves the names file in an unknown state: stop trusting the set.
        if (operation_status == SceneServiceStatus::RecoveryRequired) names_loaded_ = false;
        fail_or_rollback(operation_status);
        break;
      }
      if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
      names_ = candidate;
      if (operation_ == SceneOperation::RenameScene) {
        // A rename moves no pointer and changes no live scene: no rebuild, the UI stays put.
        finish_resume(true);
        break;
      }
      phase_ = SceneServicePhase::CommitPointer;
      break;
    }
    case SceneServicePhase::CommitPointer:
      operation_status = write_pointer_atomic(target_scene_);
      if (operation_status != SceneServiceStatus::Succeeded &&
          operation_status != SceneServiceStatus::SucceededWithBackup) {
        if (operation_ == SceneOperation::CreateScene) {
          // The pointer did not move, so the new name must not outlive this failed create.
          status_ = operation_status;
          phase_ = SceneServicePhase::RestoreNames;
        } else {
          fail_or_rollback(operation_status);
        }
      } else {
        if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
        active_scene_ = target_scene_;
        phase_ = SceneServicePhase::Rebuild;
      }
      break;
    case SceneServicePhase::Rebuild:
      lifecycle_.rebuild_ui(lifecycle_.context);
      finish_resume(true);
      break;
    case SceneServicePhase::RestoreNames: {
      // Rewrites the names file without the slot that was just named. If that write fails the
      // in-memory set keeps the new name (the last state written); the create's own status
      // is still reported.
      const SceneServiceStatus failure = status_;
      SceneNames previous = names_;
      previous.occupied_mask = static_cast<std::uint16_t>(previous.occupied_mask &
                                                          ~(1U << target_scene_));
      previous.names[target_scene_][0] = '\0';
      const SceneServiceStatus restored = write_names_atomic(previous);
      if (restored == SceneServiceStatus::Succeeded ||
          restored == SceneServiceStatus::SucceededWithBackup) {
        names_ = previous;
      } else {
        // The file may hold the new name while memory does not: stop trusting the set.
        names_loaded_ = false;
      }
      fail_or_rollback(failure);
      break;
    }
    case SceneServicePhase::ReadNames: {
      bool present = false;
      names_loaded_ = false;  // trusted again only when this read completes
      operation_status = read_names(present);
      if (operation_status != SceneServiceStatus::Succeeded) {
        fail(operation_status);
        break;
      }
      if (names_.occupied_count() != 0) {
        names_loaded_ = true;
        phase_ = SceneServicePhase::Finish;
        break;
      }
      if (present && names_.malformed_lines != 0) {
        // Unreadable names are never replaced: the slot files may hold real scenes.
        codec_result_ = SceneResult::InvalidValue;
        fail(SceneServiceStatus::SceneError);
        break;
      }
      names_loaded_ = true;  // absent or empty is an authoritative empty set
      // Absent or empty names file (first run): create "ALFA" in slot 0 from the defaults,
      // as one CreateScene. Without a valid previous scene there is nothing to roll back to.
      (void)suggest_name(names_, pending_name_, sizeof(pending_name_));
      operation_ = SceneOperation::CreateScene;
      target_scene_ = 0;
      previous_scene_ = active_scene_;
      rollback_needed_ = false;
      phase_ = SceneServicePhase::ResetDefaults;
      break;
    }
    case SceneServicePhase::RollbackRead:
      operation_status = read_scene(previous_scene_);
      if (operation_status != SceneServiceStatus::Succeeded) enter_failed();
      else phase_ = SceneServicePhase::RollbackApply;
      break;
    case SceneServicePhase::RollbackApply:
      operation_status = apply_staging();
      if (operation_status != SceneServiceStatus::Succeeded) {
        enter_failed();
      } else {
        active_scene_ = previous_scene_;
        lifecycle_.rebuild_ui(lifecycle_.context);
        phase_ = SceneServicePhase::Resume;
      }
      break;
    case SceneServicePhase::Resume:
      finish_resume(false);
      break;
    case SceneServicePhase::Failed:
      break;  // Busy and unquiesced until acknowledge_recovery(); no storage write happens here.
    case SceneServicePhase::RecoveryQuiesce:
      quiesce_sequence_ = lifecycle_.request_quiesce(lifecycle_.context, now_ms,
                                                     quiesce_timeout_ms_);
      if (quiesce_sequence_ == 0) { enter_failed(); break; }
      quiesce_requested_ = true;
      phase_ = SceneServicePhase::RecoveryWait;
      break;
    case SceneServicePhase::RecoveryWait:
      if (lifecycle_.quiesce_timed_out(lifecycle_.context, quiesce_sequence_, now_ms)) {
        lifecycle_.cancel_quiesce(lifecycle_.context, quiesce_sequence_);
        enter_failed();
      } else if (lifecycle_.quiesced(lifecycle_.context, quiesce_sequence_)) {
        phase_ = SceneServicePhase::RecoveryRead;
      }
      break;
    case SceneServicePhase::RecoveryRead:
      operation_status = read_scene(active_scene_);
      if (operation_status != SceneServiceStatus::Succeeded) enter_failed();
      else phase_ = SceneServicePhase::RecoveryApply;
      break;
    case SceneServicePhase::RecoveryApply:
      // The pointer already names active_scene_ (it is only advanced after a successful
      // apply), so re-applying the stored scene in place completes the recovery.
      operation_status = apply_staging();
      if (operation_status != SceneServiceStatus::Succeeded) {
        enter_failed();
      } else {
        lifecycle_.rebuild_ui(lifecycle_.context);
        status_ = SceneServiceStatus::Succeeded;
        phase_ = SceneServicePhase::Resume;
      }
      break;
    case SceneServicePhase::Finish:
      finish_resume(status_ == SceneServiceStatus::Busy);
      break;
    case SceneServicePhase::ReadUserSettings:
      operation_status = read_user_settings();
      finish_resume(operation_status == SceneServiceStatus::Succeeded);
      if (operation_status != SceneServiceStatus::Succeeded) status_ = operation_status;
      break;
    case SceneServicePhase::WriteUserSettings:
      operation_status = write_user_settings_atomic();
      if (operation_status != SceneServiceStatus::Succeeded &&
          operation_status != SceneServiceStatus::SucceededWithBackup) {
        fail(operation_status);
        break;
      }
      if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
      finish_resume(true);
      break;
    case SceneServicePhase::Idle:
      break;
  }
  return status_;
}

bool SceneService::acknowledge_recovery() noexcept {
  if (phase_ != SceneServicePhase::Failed) return false;
  status_ = SceneServiceStatus::Busy;
  phase_ = SceneServicePhase::RecoveryQuiesce;
  return true;
}

SceneDto* SceneService::lend_staging() noexcept { return busy() ? nullptr : &staging_; }

bool SceneService::set_user_settings(const UserSettings& settings) noexcept {
  if (busy()) return false;
  user_settings_ = settings;
  return true;
}

SceneServiceStatus SceneService::read_whole_file(SceneFileId id, char* buffer,
                                                 std::size_t capacity, std::size_t& count,
                                                 bool& present) noexcept {
  count = 0;
  present = false;
  bool exists = false;
  if (!file_exists(id, exists)) return SceneServiceStatus::StorageError;
  if (!exists) return SceneServiceStatus::Succeeded;
  SceneReader reader{};
  if (storage_.open_read(storage_.context, id, reader) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  present = true;
  SceneReadStatus status = SceneReadStatus::Byte;
  char byte = 0;
  // One byte beyond capacity is read to detect oversize without truncating silently.
  while ((status = reader.read(reader.context, byte)) == SceneReadStatus::Byte) {
    if (count == capacity) {
      status = SceneReadStatus::Error;
      break;
    }
    buffer[count++] = byte;
  }
  const auto closed = storage_.close(storage_.context, id);
  if (status == SceneReadStatus::Error || status == SceneReadStatus::Missing ||
      closed != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  return status == SceneReadStatus::End ? SceneServiceStatus::Succeeded
                                       : SceneServiceStatus::StorageError;
}

SceneServiceStatus SceneService::read_names(bool& present) noexcept {
  const auto recovered = recover_file(scene_file(SceneFileKind::SceneNames, 0),
                                      scene_file(SceneFileKind::SceneNamesTemp, 0),
                                      scene_file(SceneFileKind::SceneNamesBackup, 0));
  if (recovered != SceneServiceStatus::Succeeded) return recovered;
  char text[kSceneNamesMaxBytes]{};
  std::size_t count = 0;
  const auto read = read_whole_file(scene_file(SceneFileKind::SceneNames, 0), text,
                                    sizeof(text), count, present);
  // Oversize or unreadable text is a storage error, never an empty set (no auto-create).
  if (read != SceneServiceStatus::Succeeded) return read;
  names_.clear();
  if (!present) return SceneServiceStatus::Succeeded;
  if (parse_scene_names(text, count, names_) != SceneNamesResult::Ok)
    return SceneServiceStatus::StorageError;
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::write_names_atomic(const SceneNames& candidate) noexcept {
  char text[kSceneNamesMaxBytes]{};
  std::size_t length = 0;
  if (encode_scene_names(candidate, text, sizeof(text), length) != SceneNamesResult::Ok)
    return SceneServiceStatus::ApplyError;
  return write_text_atomic(scene_file(SceneFileKind::SceneNames, 0),
                           scene_file(SceneFileKind::SceneNamesTemp, 0),
                           scene_file(SceneFileKind::SceneNamesBackup, 0), text, length);
}

SceneServiceStatus SceneService::write_text_atomic(SceneFileId target, SceneFileId temp,
                                                   SceneFileId backup, const char* text,
                                                   std::size_t length) noexcept {
  auto result = recover_file(target, temp, backup);
  if (result != SceneServiceStatus::Succeeded) return result;
  if (!remove_if_present(temp)) return SceneServiceStatus::StorageError;

  SceneWriter writer{};
  if (storage_.open_write(storage_.context, temp, writer) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  const bool written = writer.write != nullptr &&
                       writer.write(writer.context, text, length) == SceneWriteStatus::Written;
  const auto write_close = storage_.close(storage_.context, temp);
  if (!written || write_close != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }

  // Verify the temp file by reading it back before it can replace the target.
  char verify[kSceneNamesMaxBytes]{};
  std::size_t verified_count = 0;
  bool present = false;
  if (read_whole_file(temp, verify, sizeof(verify), verified_count, present) !=
          SceneServiceStatus::Succeeded ||
      !present || verified_count != length || std::memcmp(verify, text, length) != 0) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }

  bool target_exists = false;
  if (!file_exists(target, target_exists)) return SceneServiceStatus::StorageError;
  if (target_exists && storage_.rename(storage_.context, target, backup) != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (storage_.rename(storage_.context, temp, target) != SceneStorageStatus::Ok) {
    if (target_exists && storage_.rename(storage_.context, backup, target) != SceneStorageStatus::Ok)
      return SceneServiceStatus::RecoveryRequired;
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (target_exists && !remove_if_present(backup)) {
    backup_retained_ = true;
    return SceneServiceStatus::SucceededWithBackup;
  }
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::read_user_settings() noexcept {
  const auto recovered = recover_file(scene_file(SceneFileKind::UserSettings, 0),
                                      scene_file(SceneFileKind::UserSettingsTemp, 0),
                                      scene_file(SceneFileKind::UserSettingsBackup, 0));
  if (recovered != SceneServiceStatus::Succeeded) return recovered;
  char text[kUserSettingsMaxBytes]{};
  std::size_t count = 0;
  bool present = false;
  const auto read = read_whole_file(scene_file(SceneFileKind::UserSettings, 0), text,
                                    sizeof(text), count, present);
  if (read != SceneServiceStatus::Succeeded) return read;
  if (!present) {
    // Missing file: defaults apply and the caller may save them explicitly.
    user_settings_ = UserSettings{};
    user_settings_present_ = false;
    codec_result_ = SceneResult::Missing;
    return SceneServiceStatus::Succeeded;
  }
  UserSettings loaded = user_settings_;
  const auto parsed = parse_user_settings(text, count, loaded);
  if (parsed != UserSettingsResult::Ok) {
    // Invalid persisted text is rejected; the current settings remain unchanged.
    codec_result_ = SceneResult::InvalidValue;
    return SceneServiceStatus::SceneError;
  }
  user_settings_ = loaded;
  user_settings_present_ = true;
  codec_result_ = SceneResult::Ok;
  return SceneServiceStatus::Succeeded;
}

SceneServiceStatus SceneService::write_user_settings_atomic() noexcept {
  const auto target = scene_file(SceneFileKind::UserSettings, 0);
  const auto temp = scene_file(SceneFileKind::UserSettingsTemp, 0);
  const auto backup = scene_file(SceneFileKind::UserSettingsBackup, 0);
  auto result = recover_file(target, temp, backup);
  if (result != SceneServiceStatus::Succeeded) return result;
  if (!remove_if_present(temp)) return SceneServiceStatus::StorageError;

  char text[kUserSettingsMaxBytes]{};
  std::size_t length = 0;
  if (encode_user_settings(user_settings_, text, sizeof(text), length) != UserSettingsResult::Ok)
    return SceneServiceStatus::ApplyError;

  SceneWriter writer{};
  if (storage_.open_write(storage_.context, temp, writer) != SceneStorageStatus::Ok)
    return SceneServiceStatus::StorageError;
  const bool written = writer.write != nullptr &&
                       writer.write(writer.context, text, length) == SceneWriteStatus::Written;
  const auto write_close = storage_.close(storage_.context, temp);
  if (!written || write_close != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }

  // Verify the temp file by reading it back before it can replace the target.
  char verify[kUserSettingsMaxBytes]{};
  std::size_t verified_count = 0;
  bool present = false;
  if (read_whole_file(temp, verify, sizeof(verify), verified_count, present) !=
          SceneServiceStatus::Succeeded ||
      !present || verified_count != length || std::memcmp(verify, text, length) != 0) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }

  bool target_exists = false;
  if (!file_exists(target, target_exists)) return SceneServiceStatus::StorageError;
  if (target_exists && storage_.rename(storage_.context, target, backup) != SceneStorageStatus::Ok) {
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (storage_.rename(storage_.context, temp, target) != SceneStorageStatus::Ok) {
    if (target_exists && storage_.rename(storage_.context, backup, target) != SceneStorageStatus::Ok)
      return SceneServiceStatus::RecoveryRequired;
    remove_if_present(temp);
    return SceneServiceStatus::StorageError;
  }
  if (target_exists && !remove_if_present(backup)) {
    backup_retained_ = true;
    return SceneServiceStatus::SucceededWithBackup;
  }
  user_settings_present_ = true;
  return SceneServiceStatus::Succeeded;
}

}  // namespace orbit
