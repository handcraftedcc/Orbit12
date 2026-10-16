#include "persistence/scene_service.hpp"

#include <cstring>

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
      active_scene_(active_scene < kSceneCount ? active_scene : 0),
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

bool SceneService::request(SceneOperation operation, std::uint8_t target_scene) noexcept {
  if (busy()) return false;
  const bool settings_op = operation == SceneOperation::SaveUserSettings ||
                           operation == SceneOperation::LoadUserSettings;
  if (!valid_ports() || (operation != SceneOperation::Reset && operation != SceneOperation::Save &&
                         !settings_op && target_scene >= kSceneCount)) {
    status_ = SceneServiceStatus::Rejected;
    return false;
  }
  operation_ = operation;
  target_scene_ = target_scene;
  previous_scene_ = active_scene_;
  if (operation_ == SceneOperation::Switch && target_scene_ == active_scene_)
    operation_ = SceneOperation::Save;
  backup_retained_ = false;
  quiesce_requested_ = false;
  rollback_needed_ = operation_ == SceneOperation::Switch || operation_ == SceneOperation::Load ||
                     operation_ == SceneOperation::Reset;
  pointer_commit_ = false;
  target_missing_ = false;
  codec_result_ = SceneResult::Ok;
  status_ = SceneServiceStatus::Busy;
  phase_ = SceneServicePhase::Stop;
  return true;
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
  if (read_status != SceneReadStatus::End || count < 2 || text[count - 1] != '\n')
    return SceneServiceStatus::SceneError;
  std::size_t digits = count - 1;
  if (digits != 0 && text[digits - 1] == '\r') --digits;
  if (digits == 0 || digits > 2 || text[0] < '1' || text[0] > '9' ||
      (digits == 2 && (text[0] != '1' || text[1] != '0')))
    return SceneServiceStatus::SceneError;
  if (digits == 1 && count > 2 && text[1] != '\r') return SceneServiceStatus::SceneError;
  scene = static_cast<std::uint8_t>(text[0] - '1');
  if (digits == 2) scene = 9;
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
  char expected[3]{};
  const std::uint8_t expected_size = scene == 9 ? 3 : 2;
  expected[0] = scene == 9 ? '1' : static_cast<char>('1' + scene);
  if (scene == 9) expected[1] = '0';
  expected[expected_size - 1] = '\n';
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
      actual_size != expected_size || std::memcmp(actual, expected, expected_size) != 0) {
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
        phase_ = operation_ == SceneOperation::LoadActive ? SceneServicePhase::ReadActive :
                 operation_ == SceneOperation::Load ? SceneServicePhase::ReadTarget :
                 operation_ == SceneOperation::SaveUserSettings ? SceneServicePhase::WriteUserSettings :
                 operation_ == SceneOperation::LoadUserSettings ? SceneServicePhase::ReadUserSettings :
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
      else phase_ = SceneServicePhase::ResetDefaults;
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
      operation_status = write_scene_atomic(operation_ == SceneOperation::Switch ? target_scene_
                                                                                 : active_scene_);
      if (operation_status == SceneServiceStatus::Succeeded ||
          operation_status == SceneServiceStatus::SucceededWithBackup) {
        if (operation_status == SceneServiceStatus::SucceededWithBackup) backup_retained_ = true;
        phase_ = operation_ == SceneOperation::Switch ? SceneServicePhase::CommitPointer
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
    case SceneServicePhase::CommitPointer:
      operation_status = write_pointer_atomic(target_scene_);
      if (operation_status != SceneServiceStatus::Succeeded &&
          operation_status != SceneServiceStatus::SucceededWithBackup) {
        fail_or_rollback(operation_status);
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
