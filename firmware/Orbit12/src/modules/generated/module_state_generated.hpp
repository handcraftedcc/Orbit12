// GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py.
// Processor state union (ModuleState::storage). Generated from module.json state entries.
// Include modules/common/module_state.hpp or input_state.hpp (this file depends on the core structs there).
#pragma once
#include <cstddef>
#include <cstdint>


namespace orbit {
union ModuleStorage {
    EmptyModuleState empty;
    ArpModuleState arp;
    ArpWalkModuleState arp_walk;
    BounceModuleState bounce;
    ChordModuleState chord;
    ChopperModuleState chopper;
    DelayModuleState delay;
    DropModuleState drop;
    EuclidModuleState euclid;
    GateModuleState gate;
    LatchModuleState latch;
    PickModuleState pick;
    QuantizeModuleState quantize;
    RandomizeModuleState randomize;
    RetriggerModuleState retrigger;
    StrumModuleState strum;
    TransposeModuleState transpose;
    WanderModuleState wander;
    constexpr ModuleStorage() : empty{} {}
};
static_assert(sizeof(EmptyModuleState) == 1, "empty: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ArpModuleState) == 264, "arp: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ArpWalkModuleState) == 320, "arp_walk: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(BounceModuleState) == 432, "bounce: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ChordModuleState) == 56, "chord: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(ChopperModuleState) == 128, "chopper: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(DelayModuleState) == 632, "delay: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(DropModuleState) == 16, "drop: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(EuclidModuleState) == 184, "euclid: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(GateModuleState) == 116, "gate: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(LatchModuleState) == 52, "latch: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(PickModuleState) == 108, "pick: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(QuantizeModuleState) == 488, "quantize: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(RandomizeModuleState) == 76, "randomize: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(RetriggerModuleState) == 64, "retrigger: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(StrumModuleState) == 88, "strum: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(TransposeModuleState) == 40, "transpose: state.bytes in module.json disagrees with sizeof");
static_assert(sizeof(WanderModuleState) == 144, "wander: state.bytes in module.json disagrees with sizeof");
inline constexpr std::size_t kGeneratedModuleStateMaxBytes = 632;  // core plus modules compiled into the tree
static_assert(sizeof(ModuleStorage) >= kGeneratedModuleStateMaxBytes, "union smaller than a member");
static_assert(sizeof(ModuleStorage) - kGeneratedModuleStateMaxBytes < alignof(ModuleStorage), "union padding exceeds its alignment");
static_assert(alignof(ModuleStorage) <= alignof(std::int64_t), "state alignment above the chain budget");
}  // namespace orbit
