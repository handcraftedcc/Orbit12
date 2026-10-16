#include "fs_format.hpp"

#include <cstring>

namespace orbit {
namespace fsformat {
namespace {

// Writes `text` to `name`, reads it back and requires identical bytes.
bool write_and_verify(const FsBackend& backend, const char* name, const char* text) noexcept {
  const std::size_t length = std::strlen(text);
  if (!backend.write_file(backend.context, name, text, length)) return false;
  char buffer[kTextCapacity]{};
  std::size_t got = 0;
  if (backend.read_file(backend.context, name, buffer, sizeof(buffer), got) != FileRead::Ok)
    return false;
  return got == length && std::memcmp(buffer, text, length) == 0;
}

bool stamp_present(const FsBackend& backend) noexcept {
  const std::size_t length = std::strlen(kStampText);
  char buffer[kTextCapacity]{};
  std::size_t got = 0;
  if (backend.read_file(backend.context, kStampName, buffer, sizeof(buffer), got) != FileRead::Ok)
    return false;
  return got == length && std::memcmp(buffer, kStampText, length) == 0;
}

}  // namespace

bool mount_verified(const FsBackend& backend) noexcept {
  if (!backend.mount(backend.context)) {
    backend.unmount(backend.context);
    return false;
  }
  if (!stamp_present(backend)) {
    // Mounted by LittleFS but not produced by a completed, verified format: not usable.
    backend.unmount(backend.context);
    return false;
  }
  return true;
}

FormatResult format_verified(const FsBackend& backend) noexcept {
  backend.unmount(backend.context);
  if (!backend.format(backend.context)) {
    backend.unmount(backend.context);
    return FormatResult::FormatFailed;
  }
  if (!backend.mount(backend.context)) {
    backend.unmount(backend.context);
    return FormatResult::MountFailed;
  }
  // Probe: a small file written, read back, and removed. A silent short write or a close
  // failure shows up as a content mismatch and fails the format.
  if (!write_and_verify(backend, kProbeName, kProbeText) ||
      !backend.remove_file(backend.context, kProbeName)) {
    backend.unmount(backend.context);
    return FormatResult::VerifyFailed;
  }
  // The stamp is the last write: only a completed and verified format carries it.
  if (!write_and_verify(backend, kStampName, kStampText)) {
    backend.unmount(backend.context);
    return FormatResult::VerifyFailed;
  }
  return FormatResult::Ok;
}

}  // namespace fsformat
}  // namespace orbit
