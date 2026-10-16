#pragma once
#include "basics.hpp"

namespace orbit {
// Source: circuitpython-source/src/apps/Orion/modules/euclid.py. Six lanes share one held-note
// order; PULSES, LENGTH and ROT address the lane selected by LANE. Parameter order is the
// ParamSpec order used by set_euclid_param. Persisted attributes: pulses, lengths, rotations.
bool euclid_step(std::uint32_t step, std::uint32_t pulses, std::uint32_t steps) noexcept;
void construct_euclid_state(ModuleState& state) noexcept;
void process_euclid(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_euclid_state(ModuleState&, ResetReason) noexcept;
bool set_euclid_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects) noexcept;
void euclid_attributes(ModuleState&, AttributeVisitor&) noexcept;
const ModuleDescriptor& euclid_module_descriptor() noexcept;
}  // namespace orbit
