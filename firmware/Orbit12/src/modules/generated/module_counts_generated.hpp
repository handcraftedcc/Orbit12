// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Module counts (kGeneratedProcessorCount is the registry size).
#pragma once
#include <cstdint>

namespace orbit {
inline constexpr std::uint8_t kGeneratedCoreProcessorCount = 18;
#ifdef ORBIT_NATIVE_CORE_ONLY
inline constexpr std::uint8_t kGeneratedProcessorCount = 18;  // native tests: shipped core only
inline constexpr std::uint8_t kGeneratedInputCount = 6;
#else
inline constexpr std::uint8_t kGeneratedProcessorCount = 18;  // core plus enabled user modules
inline constexpr std::uint8_t kGeneratedInputCount = 6;
#endif
}  // namespace orbit
