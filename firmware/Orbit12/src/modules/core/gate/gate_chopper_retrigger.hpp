#pragma once
#include "../../common/module_state.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/gate.py, chopper.py and
// retrigger.py. Parameter order is the C++ ParamSpec order used by set_param.
void construct_gate_state(ModuleState& state) noexcept;
void construct_chopper_state(ModuleState& state) noexcept;
void construct_retrigger_state(ModuleState& state) noexcept;
void process_gate(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_chopper(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_retrigger(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_gate_state(ModuleState&, ResetReason) noexcept;
void reset_chopper_state(ModuleState&, ResetReason) noexcept;
void reset_retrigger_state(ModuleState&, ResetReason) noexcept;
bool set_gate_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
bool set_chopper_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
bool set_retrigger_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& gate_module_descriptor() noexcept;
const ModuleDescriptor& chopper_module_descriptor() noexcept;
const ModuleDescriptor& retrigger_module_descriptor() noexcept;
}  // namespace orbit
