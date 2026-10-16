#pragma once

#include "modules/registry.hpp"
#include "output.hpp"

namespace orbit {
enum class ChainOperation : std::uint8_t { Next, Add, OutNext, OutSource, Skip };
enum class ChainSource : std::uint8_t { Previous = 0, Input = 1, Slot1 = 2, Slot2 = 3, Slot3 = 4, Slot4 = 5, Slot5 = 6 };
// `call` is the append() sequence number within a pass; drain() replays calls in
// order (offs before ons within a call) so a later slot's off cannot overtake an
// earlier slot's on.
struct RoutedNote { Note note{}; std::uint8_t channel = 0; std::uint8_t call = 0; };
class OutputCollector {
 public:
  static constexpr std::uint16_t kCapacity = kOutputCapacity;
  bool append(const NoteBatch& notes, std::uint8_t channel) noexcept;
  bool drain(Output& output) noexcept;
  void clear() noexcept { off_count_ = on_count_ = 0; calls_ = 0; overflowed_ = false; }
  std::uint16_t off_count() const noexcept { return off_count_; }
  std::uint16_t on_count() const noexcept { return on_count_; }
  const RoutedNote& off_at(std::uint16_t index) const noexcept { return offs_[index]; }
  const RoutedNote& on_at(std::uint16_t index) const noexcept { return ons_[index]; }
  bool overflowed() const noexcept { return overflowed_; }
  // Chain-detected loss (module batch overflow, merge overflow): the next drain
  // takes the same exact all_notes_off recovery as a collector overflow.
  void fault() noexcept { overflowed_ = true; }
 private:
  RoutedNote offs_[kCapacity]{};
  RoutedNote ons_[kCapacity]{};
  std::uint16_t off_count_ = 0, on_count_ = 0;
  std::uint8_t calls_ = 0;
  bool overflowed_ = false;
};
struct ChainDiagnostics { std::uint32_t invalid_config = 0, scratch_overflow = 0, output_overflow = 0, batch_overflow = 0; };
class Chain {
 public:
  static constexpr std::uint8_t kSlots = 6;
  Chain() noexcept;
  ~Chain();
  Chain(const Chain&) = delete;
  Chain& operator=(const Chain&) = delete;
  bool set_module(std::uint8_t slot, const ModuleDescriptor* descriptor) noexcept;
  bool configure(std::uint8_t slot, ChainSource source, ChainOperation operation, std::uint8_t out_channel) noexcept;
  void process(const ProcessContext& context, const NoteBatch& input, OutputCollector& output) noexcept;
  void reset(ResetReason reason) noexcept;
  // Controller-facing accessors (T11b). The controller owns parameter shadows
  // and applies set_param/attributes directly against the slot state.
  const ModuleDescriptor* descriptor(std::uint8_t slot) const noexcept {
    return slot < kSlots ? slots_[slot].descriptor : nullptr;
  }
  ModuleState* state(std::uint8_t slot) noexcept {
    return slot < kSlots ? &slots_[slot].state : nullptr;
  }
  // Source Module swap: exchanges descriptor, state and routing of two slots
  // (state.swap_modules). Snapshots are invalidated; the controller resets/stops.
  bool swap_slots(std::uint8_t a, std::uint8_t b) noexcept;
  const NoteBatch& final_stream() const noexcept { return final_stream_; }
  bool recovery_required() const noexcept { return recovery_required_; }
  const ChainDiagnostics& diagnostics() const noexcept { return diagnostics_; }
 private:
  struct Slot {
    const ModuleDescriptor* descriptor = nullptr;
    ModuleState state{};
    ChainSource source = ChainSource::Previous;
    ChainOperation operation = ChainOperation::Next;
    std::uint8_t out_channel = 0;
  } slots_[kSlots]{};
  NoteBatch snapshots_[kSlots]{};
  bool snapshot_initialized_[kSlots]{};
  ChainDiagnostics diagnostics_{};
  NoteBatch final_stream_{};
  bool recovery_required_ = false;
};
static_assert(sizeof(ModuleState) * Chain::kSlots <= 128U * 1024U,
              "six largest module states must fit application memory budget");
}  // namespace orbit
