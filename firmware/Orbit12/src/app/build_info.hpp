#pragma once

// Build identity. This committed file is a placeholder. tools/arduino/build-firmware.sh
// overwrites it with the release version and git sha for the duration of the build, then
// restores this exact placeholder so the working tree stays clean.
// Keep these four declarations in the same shape: the build script rewrites the values only.

#include <cstdint>

namespace orbit::build {

inline constexpr const char kFirmwareVersion[] = "0.0.0-dev";
inline constexpr const char kGitSha[] = "unknown";
inline constexpr std::uint32_t kEngineApi = 1;
inline constexpr const char kBoardId[] = "adafruit_macropad_rp2040";

}  // namespace orbit::build
