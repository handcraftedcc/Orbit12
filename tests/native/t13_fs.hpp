#pragma once

// In-memory LittleFS stand-in for T13 persistence tests. Mutating operations
// (open_write, each write, rename, remove) are counted so a test can cut power
// after an exact number of them; after the cut every operation fails until
// reboot(). Rename overwrites its destination, as LittleFS does.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "persistence/scene_service.hpp"

namespace t13 {

constexpr std::size_t kKinds = 9;
constexpr std::size_t kScenes = 10;
constexpr std::size_t kCapacity = orbit::kSceneMaxBytes;

struct Cell {
  std::uint8_t bytes[kCapacity]{};
  std::size_t size = 0;
  bool exists = false;
};

struct Fs {
  Cell cells[kKinds][kScenes]{};
  std::size_t free_bytes = static_cast<std::size_t>(-1);  // total capacity for writes
  int cut_after = -1;      // mutating operations allowed before power loss; -1 = never
  bool powered_off = false;
  bool fail_open_write = false;
  bool drop_writes = false;  // report Written but store nothing (silent short write)
  int renames = 0;
  int mutations = 0;
  // read/write cursors (one open handle at a time, as in the platform adapter)
  Cell* reader_cell = nullptr;
  std::size_t reader_pos = 0;
  Cell* writer_cell = nullptr;

  Cell& cell(orbit::SceneFileId id) noexcept {
    return cells[static_cast<std::size_t>(id.kind)][id.scene];
  }

  void reset() noexcept {
    for (auto& kind : cells)
      for (auto& file : kind) {
        file.size = 0;
        file.exists = false;
      }
    free_bytes = static_cast<std::size_t>(-1);
    cut_after = -1;
    powered_off = false;
    fail_open_write = false;
    drop_writes = false;
    renames = 0;
    mutations = 0;
    reader_cell = nullptr;
    writer_cell = nullptr;
  }

  // Power is restored: operations work again; file contents are whatever was left.
  void reboot() noexcept {
    powered_off = false;
    cut_after = -1;
    reader_cell = nullptr;
    writer_cell = nullptr;
  }

  bool mutate() noexcept {
    if (powered_off) return false;
    if (cut_after == 0) {
      powered_off = true;
      return false;
    }
    if (cut_after > 0) --cut_after;
    ++mutations;
    return true;
  }

  void put(orbit::SceneFileId id, const char* text) noexcept {
    auto& file = cell(id);
    const std::size_t length = std::strlen(text);
    std::memcpy(file.bytes, text, length);
    file.size = length;
    file.exists = true;
  }

  bool equals(orbit::SceneFileId id, const char* text) noexcept {
    auto& file = cell(id);
    return file.exists && file.size == std::strlen(text) &&
           std::memcmp(file.bytes, text, file.size) == 0;
  }
};

inline orbit::SceneStorageStatus exists_op(void* context, orbit::SceneFileId id, bool& exists) {
  auto& fs = *static_cast<Fs*>(context);
  exists = false;
  if (fs.powered_off) return orbit::SceneStorageStatus::Error;
  exists = fs.cell(id).exists;
  return orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneReadStatus read_op(void* context, char& byte) {
  auto& fs = *static_cast<Fs*>(context);
  if (fs.powered_off || fs.reader_cell == nullptr) return orbit::SceneReadStatus::Error;
  if (fs.reader_pos >= fs.reader_cell->size) return orbit::SceneReadStatus::End;
  byte = static_cast<char>(fs.reader_cell->bytes[fs.reader_pos++]);
  return orbit::SceneReadStatus::Byte;
}

inline orbit::SceneStorageStatus open_read_op(void* context, orbit::SceneFileId id,
                                              orbit::SceneReader& reader) {
  auto& fs = *static_cast<Fs*>(context);
  if (fs.powered_off) return orbit::SceneStorageStatus::Error;
  auto& file = fs.cell(id);
  if (!file.exists) return orbit::SceneStorageStatus::Missing;
  fs.reader_cell = &file;
  fs.reader_pos = 0;
  reader = {&fs, read_op};
  return orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneWriteStatus write_op(void* context, const char* data, std::size_t size) {
  auto& fs = *static_cast<Fs*>(context);
  if (!fs.mutate() || fs.writer_cell == nullptr) return orbit::SceneWriteStatus::Error;
  if (fs.drop_writes) return orbit::SceneWriteStatus::Written;
  if (size > fs.free_bytes) return orbit::SceneWriteStatus::Full;
  auto& file = *fs.writer_cell;
  if (size > kCapacity - file.size) return orbit::SceneWriteStatus::Full;
  std::memcpy(file.bytes + file.size, data, size);
  file.size += size;
  fs.free_bytes -= size;
  return orbit::SceneWriteStatus::Written;
}

inline orbit::SceneStorageStatus open_write_op(void* context, orbit::SceneFileId id,
                                               orbit::SceneWriter& writer) {
  auto& fs = *static_cast<Fs*>(context);
  if (!fs.mutate() || fs.fail_open_write) return orbit::SceneStorageStatus::Error;
  auto& file = fs.cell(id);
  file.exists = true;
  file.size = 0;
  fs.writer_cell = &file;
  writer = {&fs, write_op};
  return orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneStorageStatus close_op(void* context, orbit::SceneFileId) {
  auto& fs = *static_cast<Fs*>(context);
  fs.reader_cell = nullptr;
  fs.writer_cell = nullptr;
  return fs.powered_off ? orbit::SceneStorageStatus::Error : orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneStorageStatus rename_op(void* context, orbit::SceneFileId from,
                                           orbit::SceneFileId to) {
  auto& fs = *static_cast<Fs*>(context);
  if (!fs.mutate()) return orbit::SceneStorageStatus::Error;
  auto& source = fs.cell(from);
  if (!source.exists) return orbit::SceneStorageStatus::Missing;
  ++fs.renames;
  auto& destination = fs.cell(to);
  std::memcpy(destination.bytes, source.bytes, source.size);
  destination.size = source.size;
  destination.exists = true;
  source.exists = false;
  source.size = 0;
  return orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneStorageStatus remove_op(void* context, orbit::SceneFileId id) {
  auto& fs = *static_cast<Fs*>(context);
  if (!fs.mutate()) return orbit::SceneStorageStatus::Error;
  auto& file = fs.cell(id);
  if (!file.exists) return orbit::SceneStorageStatus::Missing;
  file.exists = false;
  file.size = 0;
  return orbit::SceneStorageStatus::Ok;
}

inline orbit::SceneStoragePort port(Fs& fs) noexcept {
  orbit::SceneStoragePort storage{};
  storage.context = &fs;
  storage.exists = exists_op;
  storage.open_read = open_read_op;
  storage.open_write = open_write_op;
  storage.close = close_op;
  storage.rename = rename_op;
  storage.remove = remove_op;
  return storage;
}

// One shared instance: the full file table is about 2.4 MB and must not sit on the stack.
inline Fs& shared_fs() noexcept {
  static Fs fs;
  return fs;
}

}  // namespace t13
