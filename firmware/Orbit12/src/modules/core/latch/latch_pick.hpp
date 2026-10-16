#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/latch.py and pick.py. Parameter order is the
// ParamSpec order used by set_param (main parms only; source/out parms belong to the chain).
void construct_latch_state(ModuleState& state) noexcept;
void process_latch(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_latch_state(ModuleState&, ResetReason) noexcept;
bool set_latch_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& latch_module_descriptor() noexcept;

void construct_pick_state(ModuleState& state) noexcept;
void process_pick(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_pick_state(ModuleState&, ResetReason) noexcept;
bool set_pick_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& pick_module_descriptor() noexcept;
}  // namespace orbit
