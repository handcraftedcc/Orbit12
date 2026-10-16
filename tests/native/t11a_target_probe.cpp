// Compile with the pinned arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m0plus
// -mthumb -O2 -fno-exceptions -fno-rtti -Ifirmware/Orbit12/src -S.
// Inspect both functions: publication must use inline ldr/str + dmb and no
// __atomic runtime calls, spinlocks, or backwards retry branches.
#include "app/bridge.hpp"

orbit::SpscQueue<orbit::PhysicalEvent, 64> target_probe_queue;
extern "C" bool bridge_push(orbit::PhysicalEvent event) {
  return target_probe_queue.try_push(event);
}
extern "C" bool bridge_pop(orbit::PhysicalEvent& event) {
  return target_probe_queue.try_pop(event);
}
