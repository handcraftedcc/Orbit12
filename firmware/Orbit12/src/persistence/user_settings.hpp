#pragma once

#include <cstddef>
#include <cstdint>

namespace orbit {

// Source: apps/Orion/core/user_settings.py. The file holds text lines
// "name,value"; only key_brightness is persisted (KEYBRI, float 0..1, default
// 1.0 as NeoPixels.brightness_multiplier). Unknown names are ignored.
constexpr std::size_t kUserSettingsMaxBytes = 128;
constexpr std::size_t kUserSettingsLineChars = 48;
constexpr float kUserKeyBrightnessDefault = 1.0f;
constexpr float kUserKeyBrightnessMin = 0.0f;
constexpr float kUserKeyBrightnessMax = 1.0f;

struct UserSettings {
  float key_brightness = kUserKeyBrightnessDefault;
};

enum class UserSettingsResult : std::uint8_t {
  Ok,
  TooLarge,     // exceeds kUserSettingsMaxBytes
  Malformed,    // line without ',' name/value shape, or non-decimal value
  OutOfRange,   // key_brightness outside 0..1 (rejected, never clamped)
  OutputFull    // encoder output buffer too small
};

// Strict parser: every key_brightness value must be a finite decimal in 0..1;
// invalid persistent values are rejected, not silently replaced.
UserSettingsResult parse_user_settings(const char* data, std::size_t length,
                                       UserSettings& out) noexcept;

// Writes "key_brightness,<value>\n" with at most six fractional digits
// (trailing zeros removed). Values within 1e-6 are equal after round-trip.
UserSettingsResult encode_user_settings(const UserSettings& settings, char* output,
                                        std::size_t capacity, std::size_t& written) noexcept;

}  // namespace orbit
