#pragma once

#include <cstddef>
#include <cstdint>

// Explicit, operator-requested format of the LittleFS partition (host-compilable).
//
// The firmware never formats on its own. The only path is the maintenance FORMAT frame
// (maintenance_protocol.hpp), which calls format_verified() through the storage adapter.
// The platform adapter supplies the LittleFS calls through FsBackend; this file has no
// Arduino or LittleFS includes.
//
// Definition of "formatted": the partition mounts without formatting AND the root-level
// stamp file exists with the exact stamp text. The stamp is written last, after the probe
// file has been written, read back and removed. A partition without the stamp is reported
// unmounted, even if LittleFS itself mounts it, because it was not produced by a completed
// and verified format (for example after a power cut during the format).
namespace orbit {
namespace fsformat {

constexpr char kStampName[] = "/orbit12.fs";
constexpr char kStampText[] = "ORBIT12FS1\n";
constexpr char kProbeName[] = "/fmt_probe.tmp";
constexpr char kProbeText[] = "orbit12 format probe 0123456789\n";
constexpr std::size_t kTextCapacity = 64;

enum class FileRead : std::uint8_t { Missing, Ok, Error };

// Operations supplied by the platform adapter. Every callback must leave no file handle open
// when it returns. Each function pointer must be set.
struct FsBackend {
  void* context = nullptr;
  // Erases and initializes the partition (LittleFS format). Leaves it unmounted.
  bool (*format)(void* context) = nullptr;
  // Mounts the existing filesystem. Never formats.
  bool (*mount)(void* context) = nullptr;
  // Unmounts if mounted; safe to call when not mounted.
  void (*unmount)(void* context) = nullptr;
  // Creates or replaces a whole file. True only when every byte was accepted and the file closed.
  bool (*write_file)(void* context, const char* name, const char* text, std::size_t length) = nullptr;
  // Reads a whole file into `out`. Ok only when the content fits in `capacity`.
  FileRead (*read_file)(void* context, const char* name, char* out, std::size_t capacity,
                        std::size_t& length) = nullptr;
  // Removes a file. True when the file is absent afterwards.
  bool (*remove_file)(void* context, const char* name) = nullptr;
};

enum class FormatResult : std::uint8_t {
  Ok,           // formatted, mounted, probe verified, stamp verified
  FormatFailed, // partition could not be formatted (or the adapter refused: window closed)
  MountFailed,  // formatted, but the filesystem did not mount afterwards
  VerifyFailed  // mounted, but a probe or stamp write/read-back/remove did not match
};

// Mounts without formatting and requires the stamp. On false the partition is left unmounted.
bool mount_verified(const FsBackend& backend) noexcept;

// Format, mount, verify (probe write/read/remove, then stamp write/read). On any failure the
// partition is left unmounted and the result says at which step it failed.
FormatResult format_verified(const FsBackend& backend) noexcept;

}  // namespace fsformat
}  // namespace orbit
