#pragma once

#include <cstdint>

#include "persistence/scene_codec.hpp"
#include "persistence/user_settings.hpp"

namespace orbit {

enum class SceneFileKind : std::uint8_t {
  Scene, SceneTemp, SceneBackup, ActivePointer, ActiveTemp, ActiveBackup,
  UserSettings, UserSettingsTemp, UserSettingsBackup
};
struct SceneFileId {
  SceneFileKind kind = SceneFileKind::Scene;
  std::uint8_t scene = 0;
};
enum class SceneStorageStatus : std::uint8_t { Ok, Missing, Error };
struct SceneStoragePort {
  void* context = nullptr;
  SceneStorageStatus (*exists)(void*, SceneFileId, bool&) = nullptr;
  SceneStorageStatus (*open_read)(void*, SceneFileId, SceneReader&) = nullptr;
  SceneStorageStatus (*open_write)(void*, SceneFileId, SceneWriter&) = nullptr;
  SceneStorageStatus (*close)(void*, SceneFileId) = nullptr;
  SceneStorageStatus (*rename)(void*, SceneFileId, SceneFileId) = nullptr;
  SceneStorageStatus (*remove)(void*, SceneFileId) = nullptr;
};

// Application callbacks execute on core 0. Quiesce callbacks are adapters for
// MaintenanceHandshake; none may wait. Scene callbacks update live state only
// while the service owns a stopped/quiesced maintenance window.
struct SceneLifecyclePort {
  void* context = nullptr;
  bool (*is_stopped)(void*) = nullptr;
  void (*stop_release_drain)(void*) = nullptr;
  std::uint32_t (*request_quiesce)(void*, std::uint32_t now_ms,
                                   std::uint32_t timeout_ms) = nullptr;
  bool (*quiesced)(void*, std::uint32_t sequence) = nullptr;
  bool (*quiesce_timed_out)(void*, std::uint32_t sequence,
                            std::uint32_t now_ms) = nullptr;
  void (*cancel_quiesce)(void*, std::uint32_t sequence) = nullptr;
  void (*resume)(void*, std::uint32_t sequence) = nullptr;
  bool (*capture)(void*, SceneDto&) = nullptr;
  bool (*reset_defaults)(void*) = nullptr;
  bool (*prepare_scene)(void*) = nullptr;
  bool (*apply_globals)(void*, const GlobalState&, std::uint8_t fields) = nullptr;
  bool (*construct_module)(void*, std::uint8_t slot, const char* id,
                           std::uint16_t version, bool unknown_module) = nullptr;
  bool (*apply_parameter)(void*, std::uint8_t slot,
                         const SceneParameter&) = nullptr;
  bool (*apply_attribute)(void*, std::uint8_t slot,
                         const SceneAttribute&) = nullptr;
  bool (*finish_scene)(void*) = nullptr;
  void (*rebuild_ui)(void*) = nullptr;
};

enum class SceneOperation : std::uint8_t {
  Save, Switch, CopyCurrent, Load, LoadActive, Reset, SaveUserSettings, LoadUserSettings
};
enum class SceneServiceStatus : std::uint8_t {
  Idle, Busy, Succeeded, SucceededWithBackup, Rejected, SceneError,
  StorageError, QuiesceTimeout, ApplyError, RecoveryRequired
};
enum class SceneServicePhase : std::uint8_t {
  Idle, Stop, WaitStopped, WaitQuiesce, ReadActive, CaptureCurrent,
  SaveCurrent, ReadTarget, ResetDefaults, CaptureReset, SaveReset,
  Apply, CommitPointer, Rebuild, Resume, RollbackRead, RollbackApply,
  Finish, Failed, ReadUserSettings, WriteUserSettings, RecoveryQuiesce, RecoveryWait,
  RecoveryRead, RecoveryApply
};

class SceneService {
 public:
  SceneService(const SceneStoragePort& storage, const SceneLifecyclePort& lifecycle,
               const SceneSchemaResolver& schema, std::uint8_t active_scene = 0,
               std::uint32_t quiesce_timeout_ms = 1000) noexcept;

  bool request(SceneOperation operation, std::uint8_t target_scene = 0) noexcept;
  SceneServiceStatus poll(std::uint32_t now_ms) noexcept;
  bool busy() const noexcept { return phase_ != SceneServicePhase::Idle; }
  SceneServiceStatus status() const noexcept { return status_; }
  SceneServicePhase phase() const noexcept { return phase_; }
  std::uint8_t active_scene() const noexcept { return active_scene_; }
  SceneResult codec_result() const noexcept { return codec_result_; }
  bool backup_retained() const noexcept { return backup_retained_; }
  // Number of entries into the Failed phase (failed rollbacks and failed recoveries).
  std::uint32_t failure_count() const noexcept { return failure_count_; }

  // Failed-phase exit. Valid only in phase Failed. Entering Failed resumed the worker,
  // so this requests a fresh quiesce (the recovery read may rename files), re-reads the
  // committed active scene from storage and re-applies it in place: no reset, no reboot. Returns
  // false (and changes nothing) when the service is not Failed. The outcome is
  // reported through poll()/status(): Succeeded when the re-apply completes, or
  // RecoveryRequired when the read or re-apply fails again (the service stays Failed).
  bool acknowledge_recovery() noexcept;
  // Lends the staging SceneDto to a caller that needs one SCN1 decode buffer
  // (the CDC maintenance verify path). Returns nullptr while a scene operation is
  // in progress. Every scene operation refills the staging object before reading
  // it, so the loan never carries state across operations.
  SceneDto* lend_staging() noexcept;

  // User settings (KEYBRI). set_user_settings stages the value a
  // SaveUserSettings request will write; it is rejected while busy. After a
  // successful LoadUserSettings, user_settings() holds the loaded value (or the
  // default when no file exists, with user_settings_present() false).
  bool set_user_settings(const UserSettings& settings) noexcept;
  const UserSettings& user_settings() const noexcept { return user_settings_; }
  bool user_settings_present() const noexcept { return user_settings_present_; }

 private:
  SceneServiceStatus read_user_settings() noexcept;
  SceneServiceStatus write_user_settings_atomic() noexcept;
  SceneServiceStatus read_whole_file(SceneFileId id, char* buffer, std::size_t capacity,
                                     std::size_t& count, bool& present) noexcept;
  bool valid_ports() const noexcept;
  SceneServiceStatus capture() noexcept;
  SceneServiceStatus read_scene(std::uint8_t scene) noexcept;
  SceneServiceStatus write_scene_atomic(std::uint8_t scene) noexcept;
  SceneServiceStatus write_pointer_atomic(std::uint8_t scene) noexcept;
  SceneServiceStatus read_pointer(std::uint8_t& scene) noexcept;
  SceneServiceStatus recover_file(SceneFileId target, SceneFileId temp,
                                  SceneFileId backup) noexcept;
  SceneServiceStatus apply_staging() noexcept;
  // Operation failure: keeps the given status as the reported result, then rolls back
  // (when the operation changed live state) or fails in place. A rollback never replaces it.
  void fail_or_rollback(SceneServiceStatus status) noexcept;
  // Entry into Failed: resumes the worker quiesce, counts one failure and stays busy.
  void enter_failed() noexcept;
  void fail(SceneServiceStatus status) noexcept;
  void finish_resume(bool success) noexcept;
  bool file_exists(SceneFileId id, bool& exists) noexcept;
  bool remove_if_present(SceneFileId id) noexcept;

  SceneStoragePort storage_{};
  SceneLifecyclePort lifecycle_{};
  SceneSchemaResolver schema_{};
  SceneDto staging_{}; // The one caller-owned staging object for all codec operations.
  SceneOperation operation_ = SceneOperation::Save;
  SceneServicePhase phase_ = SceneServicePhase::Idle;
  SceneServiceStatus status_ = SceneServiceStatus::Idle;
  SceneResult codec_result_ = SceneResult::Ok;
  std::uint8_t active_scene_ = 0;
  std::uint8_t target_scene_ = 0;
  std::uint8_t previous_scene_ = 0;
  std::uint32_t quiesce_sequence_ = 0;
  std::uint32_t quiesce_timeout_ms_ = 1000;
  bool quiesce_requested_ = false;
  bool rollback_needed_ = false;
  bool backup_retained_ = false;
  bool pointer_commit_ = false;
  // Set by read_scene when the file is absent (not corrupt). A Switch to such a target
  // gets the stock defaults, saved to that scene, as Orion.load_active_scene does.
  bool target_missing_ = false;
  std::uint32_t failure_count_ = 0;
  UserSettings user_settings_{};
  bool user_settings_present_ = false;
};

}  // namespace orbit
