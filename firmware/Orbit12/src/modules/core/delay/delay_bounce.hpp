#pragma once

#include "../../common/module_state.hpp"

namespace orbit {

// Processors ported from circuitpython-source/src/apps/Orion/modules/delay.py
// and bounce.py. Deviations from the source (release safety, bounded output,
// 32-bit millisecond wrap) are listed in delay_bounce.cpp.
void construct_delay_state(ModuleState& state) noexcept;
void construct_bounce_state(ModuleState& state) noexcept;
void process_delay(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_bounce(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_delay_state(ModuleState&, ResetReason) noexcept;
void reset_bounce_state(ModuleState&, ResetReason) noexcept;
bool set_delay_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
bool set_bounce_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
const ModuleDescriptor& delay_module_descriptor() noexcept;
const ModuleDescriptor& bounce_module_descriptor() noexcept;

}  // namespace orbit
