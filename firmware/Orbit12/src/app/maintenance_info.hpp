#pragma once

// INFO reply text for the maintenance protocol ('I' frame). Pure composition: no heap, no
// Arduino headers, bounded output. The application fills InfoFields from its own state.
//
// Text (one "key=value" group per line, LF terminated):
//   INFO fw=<version> sha=<git sha> api=<engine api> board=<board id>
//   MODULES count=<n> core=<id,id,...> user=<ids, empty for now>
//   STORAGE mounted=<0|1> scenes=<named slots in memory> names_ok=<0|1>
//   STATE maint=<0 normal|1 maintenance> transport=<0|1>
//   CAPS rename=<0|1>   1: the RENAME frame is served (a host can rename a named scene)
// Sections added after the first four are optional for readers: a reader that does not know
// one ignores it, and a missing CAPS line means no optional capability.
#include <cstddef>
#include <cstdint>

#include "module_descriptors.hpp"

namespace orbit {

struct InfoFields {
  const char* firmware = "";
  const char* git_sha = "";
  std::uint32_t engine_api = 0;
  const char* board = "";
  const ModuleDescription* core_modules = nullptr;   // registry order (module_descriptors.hpp)
  std::size_t core_module_count = 0;
  bool storage_mounted = false;
  std::size_t scenes = 0;          // named slots held in memory (0 when the names are not loaded)
  bool names_loaded = false;
  bool maintenance = false;
  bool transport_running = false;
};

// Writes the text into out. Returns its length, or 0 when it does not fit (nothing is sent
// truncated).
std::size_t encode_info(const InfoFields& fields, char* out, std::size_t capacity) noexcept;

// Bound for the composed text (the reply payload). Sized for the stamped identity of a release
// build (a 40-hex sha plus "-dirty", a semver-like version) and all 24 module ids; the
// application asserts info_text_bound() of the stamped strings against it at build time.
constexpr std::size_t kInfoTextMaxBytes = 512;

constexpr std::size_t info_cstr_length(const char* text) noexcept {
  std::size_t n = 0;
  while (text[n] != '\0') ++n;
  return n;
}

// Worst-case length of encode_info() output for these identity strings and modules: every
// number at ten digits (the widest uint32), every boolean one digit.
constexpr std::size_t info_text_bound(const char* firmware, const char* git_sha, const char* board,
                                      const ModuleDescription* modules, std::size_t module_count) noexcept {
  std::size_t ids = 0;
  for (std::size_t i = 0; i < module_count; ++i) ids += info_cstr_length(modules[i].id);
  constexpr std::size_t kDigits = 10;
  return (sizeof("INFO fw=") - 1) + info_cstr_length(firmware) +
         (sizeof(" sha=") - 1) + info_cstr_length(git_sha) +
         (sizeof(" api=") - 1) + kDigits +
         (sizeof(" board=") - 1) + info_cstr_length(board) + 1 +
         (sizeof("MODULES count=") - 1) + kDigits +
         (sizeof(" core=") - 1) + ids + (module_count == 0 ? 0 : module_count - 1) +
         (sizeof(" user=") - 1) + 1 +
         (sizeof("STORAGE mounted=") - 1) + 1 +
         (sizeof(" scenes=") - 1) + kDigits +
         (sizeof(" names_ok=") - 1) + 1 + 1 +
         (sizeof("STATE maint=") - 1) + 1 +
         (sizeof(" transport=") - 1) + 1 + 1 +
         (sizeof("CAPS rename=") - 1) + 1 + 1;
}

}  // namespace orbit
