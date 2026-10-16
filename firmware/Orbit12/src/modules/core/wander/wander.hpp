#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/wander.py. Parameter order is the
// create_main_parms() order of the source (eleven controls; no button). Wander has no saved
// attributes. Deviations are listed in wander.cpp.
void construct_wander_state(ModuleState& state) noexcept;
void process_wander(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_wander_state(ModuleState&, ResetReason) noexcept;
bool set_wander_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
void wander_attributes(ModuleState&, AttributeVisitor&) noexcept;
const ModuleDescriptor& wander_module_descriptor() noexcept;
}  // namespace orbit
