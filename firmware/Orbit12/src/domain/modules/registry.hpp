#pragma once
#include "basics.hpp"

namespace orbit {
const ModuleDescriptor* find_module(const char* id) noexcept;
std::uint8_t module_count() noexcept;
const ModuleDescriptor* module_at(std::uint8_t index) noexcept;
const ModuleDescriptor& empty_module_descriptor() noexcept;
}  // namespace orbit
