#pragma once

// LittleFS adapter for SceneService and the CDC maintenance session. Arduino
// and LittleFS headers are confined to littlefs_storage.cpp; this header is
// free of library includes.
//
// File layout (LittleFS root, flat names, matching the CircuitPython names
// without their /userdata/Orbit12Orion/ directory):
//   Scene01..Scene16, scene_names, _activescene, user_settings  committed files
//   <name>.tmp                                             staged writes
//   <name>.bak                                             previous version during commit
//
// Rules:
//  - mount() mounts an existing filesystem only and requires the format stamp
//    (persistence/fs_format.hpp). It never formats: a failed mount is reported.
//  - format() is the explicit operator path (maintenance FORMAT frame only). It
//    needs the maintenance window open, and it reports the mounted state honestly.
//  - Only one file is open at a time (reader or writer), matching SceneService
//    and MaintenanceSession, which open one handle per step.
//  - Writes are refused unless the maintenance window is open. The application
//    opens the window only in its quiesced, stopped state and closes it on resume.
//  - A short write is reported as Full (LittleFS does not expose the cause).
//  - Close errors are not observable through the Arduino File API; the
//    verify-by-read step in the callers detects corrupt content.

#include "../../persistence/fs_format.hpp"
#include "../../persistence/scene_service.hpp"

namespace orbit {

class LittleFsStorage {
 public:
  // Mounts the existing LittleFS partition without formatting it.
  bool mount() noexcept;
  // Erases the partition and verifies the result (see fs_format.hpp). Refused, and
  // left unmounted, unless the maintenance window is open. Only the maintenance
  // FORMAT frame calls this.
  fsformat::FormatResult format() noexcept;
  bool mounted() const noexcept { return mounted_; }

  void set_maintenance_window(bool open) noexcept { window_open_ = open; }
  bool maintenance_window() const noexcept { return window_open_; }

  // Port for SceneService and MaintenanceSession. The context is this object.
  SceneStoragePort port() noexcept;

  // Path text for a file kind, e.g. "/Scene01.tmp". Returns length or 0 on short capacity.
  static std::size_t path_for(SceneFileId id, char* output, std::size_t capacity) noexcept;

 private:
  static SceneStorageStatus exists_op(void* self, SceneFileId id, bool& exists);
  static SceneStorageStatus open_read_op(void* self, SceneFileId id, SceneReader& reader);
  static SceneStorageStatus open_write_op(void* self, SceneFileId id, SceneWriter& writer);
  static SceneStorageStatus close_op(void* self, SceneFileId id);
  static SceneStorageStatus rename_op(void* self, SceneFileId from, SceneFileId to);
  static SceneStorageStatus remove_op(void* self, SceneFileId id);

  bool mounted_ = false;
  bool window_open_ = false;
};

}  // namespace orbit
