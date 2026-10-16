#pragma once
#include "basics.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/arp_walk.py. Parameter order is the
// get_parms() order of the source main parameters (including the RAND action button). Persisted
// fields are the twelve non-button parameters; step_values is the saved attribute.
void construct_arp_walk_state(ModuleState& state) noexcept;
void process_arp_walk(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_arp_walk_state(ModuleState&, ResetReason) noexcept;
bool set_arp_walk_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
void arp_walk_attributes(ModuleState&, AttributeVisitor&) noexcept;
const ModuleDescriptor& arp_walk_module_descriptor() noexcept;
}  // namespace orbit
