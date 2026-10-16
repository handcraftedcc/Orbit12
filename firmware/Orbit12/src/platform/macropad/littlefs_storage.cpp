#include "platform/macropad/littlefs_storage.hpp"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstring>

namespace orbit {
namespace {

// One open handle at a time: reader or writer. Single-core ownership: this adapter
// is used only by core 0 during stopped maintenance, so no synchronization is needed.
File g_file;
enum class OpenMode : std::uint8_t { None, Reader, Writer };
OpenMode g_mode = OpenMode::None;

// Writes the base name of a committed file and its suffix. Returns false if the
// capacity is short.
bool build_path(SceneFileId id, char* out, std::size_t capacity, std::size_t& length) noexcept {
  const char* suffix = "";
  SceneFileKind base = id.kind;
  switch (id.kind) {
    case SceneFileKind::SceneTemp: base = SceneFileKind::Scene; suffix = ".tmp"; break;
    case SceneFileKind::SceneBackup: base = SceneFileKind::Scene; suffix = ".bak"; break;
    case SceneFileKind::ActiveTemp: base = SceneFileKind::ActivePointer; suffix = ".tmp"; break;
    case SceneFileKind::ActiveBackup: base = SceneFileKind::ActivePointer; suffix = ".bak"; break;
    case SceneFileKind::UserSettingsTemp: base = SceneFileKind::UserSettings; suffix = ".tmp"; break;
    case SceneFileKind::UserSettingsBackup: base = SceneFileKind::UserSettings; suffix = ".bak"; break;
    default: break;
  }
  const char* name = nullptr;
  char scene_name[9] = {0};
  if (base == SceneFileKind::Scene) {
    if (id.scene >= 10) return false;
    const int number = id.scene + 1;
    std::memcpy(scene_name, "Scene", 5);
    scene_name[5] = static_cast<char>('0' + number / 10);
    scene_name[6] = static_cast<char>('0' + number % 10);
    name = scene_name;
  } else if (base == SceneFileKind::ActivePointer) {
    name = "_activescene";
  } else if (base == SceneFileKind::UserSettings) {
    name = "user_settings";
  } else {
    return false;
  }
  length = 1 + std::strlen(name) + std::strlen(suffix);
  if (length + 1 > capacity) return false;
  out[0] = '/';
  std::memcpy(out + 1, name, std::strlen(name));
  std::memcpy(out + 1 + std::strlen(name), suffix, std::strlen(suffix) + 1);
  return true;
}

// LittleFS calls behind the host-compilable format sequence. The format callback is the
// only one that erases; every other callback is read-only or a write inside the window.
bool fs_format_op(void*) { return LittleFS.format(); }

bool fs_mount_op(void*) {
  // Never format: the mount itself is non-destructive.
  LittleFSConfig config(false);
  if (!LittleFS.setConfig(config)) return false;
  return LittleFS.begin();
}

void fs_unmount_op(void*) { LittleFS.end(); }

bool fs_write_file_op(void*, const char* name, const char* text, std::size_t length) {
  if (g_mode != OpenMode::None) return false;
  File file = LittleFS.open(name, "w");
  if (!file) return false;
  const std::size_t written = file.write(reinterpret_cast<const std::uint8_t*>(text), length);
  file.close();  // a close failure is caught by the read-back in fs_format
  return written == length;
}

fsformat::FileRead fs_read_file_op(void*, const char* name, char* out, std::size_t capacity,
                                   std::size_t& length) {
  length = 0;
  if (g_mode != OpenMode::None) return fsformat::FileRead::Error;
  if (!LittleFS.exists(name)) return fsformat::FileRead::Missing;
  File file = LittleFS.open(name, "r");
  if (!file) return fsformat::FileRead::Error;
  std::size_t count = 0;
  while (file.available()) {
    const int value = file.read();
    if (value < 0 || count == capacity) {
      file.close();
      return fsformat::FileRead::Error;
    }
    out[count++] = static_cast<char>(value);
  }
  file.close();
  length = count;
  return fsformat::FileRead::Ok;
}

bool fs_remove_file_op(void*, const char* name) {
  if (g_mode != OpenMode::None) return false;
  if (LittleFS.exists(name) && !LittleFS.remove(name)) return false;
  return !LittleFS.exists(name);
}

fsformat::FsBackend littlefs_backend() noexcept {
  fsformat::FsBackend backend;
  backend.context = nullptr;
  backend.format = fs_format_op;
  backend.mount = fs_mount_op;
  backend.unmount = fs_unmount_op;
  backend.write_file = fs_write_file_op;
  backend.read_file = fs_read_file_op;
  backend.remove_file = fs_remove_file_op;
  return backend;
}

bool path_of(SceneFileId id, char* out, std::size_t capacity) noexcept {
  std::size_t length = 0;
  return build_path(id, out, capacity, length);
}

SceneReadStatus read_byte(void*, char& byte) {
  if (g_mode != OpenMode::Reader) return SceneReadStatus::Error;
  if (!g_file.available()) return SceneReadStatus::End;
  const int value = g_file.read();
  if (value < 0) return SceneReadStatus::Error;
  byte = static_cast<char>(value);
  return SceneReadStatus::Byte;
}

SceneWriteStatus write_bytes(void*, const char* data, std::size_t size) {
  if (g_mode != OpenMode::Writer) return SceneWriteStatus::Error;
  if (size == 0) return SceneWriteStatus::Written;
  const std::size_t written = g_file.write(reinterpret_cast<const std::uint8_t*>(data), size);
  if (written == size) return SceneWriteStatus::Written;
  return written < size ? SceneWriteStatus::Full : SceneWriteStatus::Error;
}

}  // namespace

std::size_t LittleFsStorage::path_for(SceneFileId id, char* output, std::size_t capacity) noexcept {
  std::size_t length = 0;
  return build_path(id, output, capacity, length) ? length : 0;
}

bool LittleFsStorage::mount() noexcept {
  if (mounted_) return true;
  // Never formats; a partition without the verified-format stamp is reported unmounted.
  mounted_ = fsformat::mount_verified(littlefs_backend());
  return mounted_;
}

fsformat::FormatResult LittleFsStorage::format() noexcept {
  // A refused call changes nothing: the filesystem stays as it was.
  if (!window_open_) return fsformat::FormatResult::FormatFailed;
  // Unmounted before the erase starts; the result below sets the flag again.
  mounted_ = false;
  if (g_mode != OpenMode::None) g_file.close();
  g_mode = OpenMode::None;
  const fsformat::FormatResult result = fsformat::format_verified(littlefs_backend());
  mounted_ = result == fsformat::FormatResult::Ok;
  return result;
}

SceneStoragePort LittleFsStorage::port() noexcept {
  SceneStoragePort storage{};
  storage.context = this;
  storage.exists = exists_op;
  storage.open_read = open_read_op;
  storage.open_write = open_write_op;
  storage.close = close_op;
  storage.rename = rename_op;
  storage.remove = remove_op;
  return storage;
}

SceneStorageStatus LittleFsStorage::exists_op(void* self, SceneFileId id, bool& exists) {
  exists = false;
  auto& storage = *static_cast<LittleFsStorage*>(self);
  char path[32];
  if (!storage.mounted_ || !path_of(id, path, sizeof(path))) return SceneStorageStatus::Error;
  exists = LittleFS.exists(path);
  return SceneStorageStatus::Ok;
}

SceneStorageStatus LittleFsStorage::open_read_op(void* self, SceneFileId id, SceneReader& reader) {
  auto& storage = *static_cast<LittleFsStorage*>(self);
  char path[32];
  if (!storage.mounted_ || g_mode != OpenMode::None || !path_of(id, path, sizeof(path)))
    return SceneStorageStatus::Error;
  if (!LittleFS.exists(path)) return SceneStorageStatus::Missing;
  g_file = LittleFS.open(path, "r");
  if (!g_file) return SceneStorageStatus::Error;
  g_mode = OpenMode::Reader;
  reader = {nullptr, read_byte};
  return SceneStorageStatus::Ok;
}

SceneStorageStatus LittleFsStorage::open_write_op(void* self, SceneFileId id, SceneWriter& writer) {
  auto& storage = *static_cast<LittleFsStorage*>(self);
  char path[32];
  // Writes are refused outside the stopped maintenance window, whatever the caller.
  if (!storage.mounted_ || !storage.window_open_ || g_mode != OpenMode::None ||
      !path_of(id, path, sizeof(path)))
    return SceneStorageStatus::Error;
  g_file = LittleFS.open(path, "w");
  if (!g_file) return SceneStorageStatus::Error;
  g_mode = OpenMode::Writer;
  writer = {nullptr, write_bytes};
  return SceneStorageStatus::Ok;
}

SceneStorageStatus LittleFsStorage::close_op(void* self, SceneFileId) {
  auto& storage = *static_cast<LittleFsStorage*>(self);
  if (!storage.mounted_) return SceneStorageStatus::Error;
  if (g_mode != OpenMode::None) g_file.close();
  g_mode = OpenMode::None;
  return SceneStorageStatus::Ok;
}

SceneStorageStatus LittleFsStorage::rename_op(void* self, SceneFileId from, SceneFileId to) {
  auto& storage = *static_cast<LittleFsStorage*>(self);
  char source[32], destination[32];
  if (!storage.mounted_ || g_mode != OpenMode::None || !path_of(from, source, sizeof(source)) ||
      !path_of(to, destination, sizeof(destination)) || !storage.window_open_)
    return SceneStorageStatus::Error;
  if (!LittleFS.exists(source)) return SceneStorageStatus::Missing;
  // LittleFS rename replaces an existing destination; the callers rely on that for commits.
  return LittleFS.rename(source, destination) ? SceneStorageStatus::Ok : SceneStorageStatus::Error;
}

SceneStorageStatus LittleFsStorage::remove_op(void* self, SceneFileId id) {
  auto& storage = *static_cast<LittleFsStorage*>(self);
  char path[32];
  if (!storage.mounted_ || g_mode != OpenMode::None || !path_of(id, path, sizeof(path)) ||
      !storage.window_open_)
    return SceneStorageStatus::Error;
  if (!LittleFS.exists(path)) return SceneStorageStatus::Missing;
  return LittleFS.remove(path) ? SceneStorageStatus::Ok : SceneStorageStatus::Error;
}

}  // namespace orbit
