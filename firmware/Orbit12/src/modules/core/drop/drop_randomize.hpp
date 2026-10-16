#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Deterministic seed hash from the Python source (utils.hash_u32). Evaluated in
// 64-bit wrapping arithmetic; bit-identical to Python for seeds below 2^59.
namespace seeded {
std::uint32_t hash_u32(std::uint64_t value) noexcept;
// hash / 2^32, as Python's utils.random_float.
double random_float(std::uint64_t seed) noexcept;
// Python utils.random_int: mode 0 yields 0..max, mode 1 yields -|max|..|max|.
std::int64_t random_int(std::uint64_t seed, std::int64_t max_value, std::uint8_t mode) noexcept;
}  // namespace seeded

void construct_drop_state(ModuleState& state) noexcept;
void construct_randomize_state(ModuleState& state) noexcept;
void process_drop(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_randomize(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_drop_state(ModuleState&, ResetReason) noexcept;
void reset_randomize_state(ModuleState&, ResetReason) noexcept;
bool set_drop_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
bool set_randomize_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& drop_module_descriptor() noexcept;
const ModuleDescriptor& randomize_module_descriptor() noexcept;
}  // namespace orbit
