#pragma once
#include "inputs.hpp"

namespace orbit {
const InputDescriptor* find_input_module(const char* id) noexcept;
std::uint8_t input_module_count() noexcept;
const InputDescriptor* input_module_at(std::uint8_t index) noexcept;
}  // namespace orbit
