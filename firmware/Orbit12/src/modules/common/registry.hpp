#pragma once
#include "module_state.hpp"

namespace orbit {
const ModuleDescriptor* find_module(const char* id) noexcept;
std::uint8_t module_count() noexcept;
const ModuleDescriptor* module_at(std::uint8_t index) noexcept;
const ModuleDescriptor& empty_module_descriptor() noexcept;
// Scene keys (SCN1 module ids, at most 10 characters) and module ids. Generated in
// modules/generated/registry_generated.cpp. Unknown names map to themselves, so the fixed slots
// (set, output, settings) and the input modules keep their names.
const char* module_scene_key(const char* module_id) noexcept;
const char* module_id_for_scene_key(const char* scene_key) noexcept;
}  // namespace orbit
