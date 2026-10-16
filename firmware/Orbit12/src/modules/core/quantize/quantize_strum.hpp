#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Processors ported from circuitpython-source/src/apps/Orion/modules/quantize.py and strum.py.
// Main parameters only (set_param indexes follow the ParamSpec order); source/out parameters
// belong to the chain. Deviations from the source are listed in quantize_strum.cpp.
void construct_quantize_state(ModuleState& state) noexcept;
void process_quantize(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_quantize_state(ModuleState&, ResetReason) noexcept;
bool set_quantize_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& quantize_module_descriptor() noexcept;

void construct_strum_state(ModuleState& state) noexcept;
void process_strum(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_strum_state(ModuleState&, ResetReason) noexcept;
bool set_strum_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& strum_module_descriptor() noexcept;
}  // namespace orbit
