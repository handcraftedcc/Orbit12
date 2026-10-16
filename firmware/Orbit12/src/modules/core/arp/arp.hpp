#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/arp.py. Parameter order is the
// ParamSpec order used by set_param (main parms only; source/out parms belong to the chain).
void construct_arp_state(ModuleState& state) noexcept;
void process_arp(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_arp_state(ModuleState&, ResetReason) noexcept;
bool set_arp_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& arp_module_descriptor() noexcept;
}  // namespace orbit
