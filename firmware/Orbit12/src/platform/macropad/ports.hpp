#pragma once
// Domain-facing port structs (function pointer + context), per contracts.md.
// MidiPort lives in domain/output.hpp; Storage ports belong to T13.
#include "domain/types.hpp"

namespace orbit {

struct ClockPort {
  void* context = nullptr;
  TimeSample (*sample)(void*) = nullptr;
  TimeSample now() const noexcept { return sample ? sample(context) : TimeSample{}; }
};

// Appends debounced edges and encoder motion since the previous poll to
// `frame` (caller clears it between passes). Returns false when there was no
// new input; never blocks.
struct InputPort {
  void* context = nullptr;
  bool (*poll)(void*, InputFrame&) = nullptr;
  bool read(InputFrame& frame) const noexcept { return poll && poll(context, frame); }
};

}  // namespace orbit
