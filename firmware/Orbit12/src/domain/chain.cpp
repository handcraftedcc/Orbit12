#include "chain.hpp"

namespace orbit {
bool OutputCollector::append(const NoteBatch& notes, std::uint8_t channel) noexcept {
  if (channel > kMidiChannelMax) { overflowed_ = true; return false; }
  const std::uint8_t call = calls_;
  if (calls_ < 255) ++calls_;
  bool ok = true;
  // Exact-duplicate collapse only: an earlier identical event is redundant
  // unless an opposite event for the same key was appended after it.
  for (std::uint16_t i = 0; i < notes.off.count; ++i) {
    const Note& n = notes.off.items[i];
    bool duplicate = false;
    for (std::uint16_t j = 0; j < off_count_ && !duplicate; ++j) {
      if (offs_[j].note.pitch != n.pitch || offs_[j].channel != channel) continue;
      duplicate = true;
      for (std::uint16_t k = 0; k < on_count_; ++k)
        if (ons_[k].note.pitch == n.pitch && ons_[k].channel == channel && ons_[k].call >= offs_[j].call) duplicate = false;
    }
    if (duplicate) continue;
    if (off_count_ >= kCapacity) { overflowed_ = true; ok = false; break; }
    offs_[off_count_++] = {n, channel, call};
  }
  for (std::uint16_t i = 0; i < notes.on.count; ++i) {
    const Note& n = notes.on.items[i];
    bool duplicate = false;
    for (std::uint16_t j = 0; j < on_count_ && !duplicate; ++j) {
      if (ons_[j].note.pitch != n.pitch || ons_[j].channel != channel) continue;
      duplicate = true;
      for (std::uint16_t k = 0; k < off_count_; ++k)
        if (offs_[k].note.pitch == n.pitch && offs_[k].channel == channel && offs_[k].call > ons_[j].call) duplicate = false;
    }
    if (duplicate) continue;
    if (on_count_ >= kCapacity) { overflowed_ = true; ok = false; break; }
    ons_[on_count_++] = {n, channel, call};
  }
  return ok;
}
bool OutputCollector::drain(Output& output) noexcept {
  if (overflowed_) {
    (void)output.all_notes_off();
    (void)output.queue_stop();
    clear();
    return false;
  }
  bool complete = true;
  // Replay in append-call order; within a call offs precede ons. Offs are never
  // blocked behind a refused on: a refused on is dropped (Output counts it) while
  // transport-level refusal (recovering / disconnected) keeps the rest queued.
  while (off_count_ || on_count_) {
    const bool take_off = off_count_ && (!on_count_ || offs_[0].call <= ons_[0].call);
    NoteBatch batch{};
    const RoutedNote head = take_off ? offs_[0] : ons_[0];
    if (take_off) batch.off.append(head.note); else batch.on.append(head.note);
    if (!output.submit(batch, head.channel)) {
      complete = false;
      if (output.recovering() || !output.connected() || take_off) break;  // keep for retry
    }
    if (take_off) { for (std::uint16_t j = 1; j < off_count_; ++j) offs_[j - 1] = offs_[j]; --off_count_; }
    else { for (std::uint16_t j = 1; j < on_count_; ++j) ons_[j - 1] = ons_[j]; --on_count_; }
  }
  return complete;
}
Chain::Chain() noexcept {
  for (std::uint8_t i = 0; i < kSlots; ++i) (void)set_module(i, &empty_module_descriptor());
}
Chain::~Chain() {
  for (auto& slot : slots_) if (slot.descriptor) slot.descriptor->destroy(slot.state);
}
bool Chain::set_module(std::uint8_t index, const ModuleDescriptor* descriptor) noexcept {
  if (index >= kSlots || !descriptor || !descriptor->construct || !descriptor->destroy || !descriptor->process) return false;
  auto& slot = slots_[index];
  if (slot.descriptor) slot.descriptor->destroy(slot.state);
  slot.descriptor = descriptor;
  descriptor->construct(slot.state);
  slot.source = ChainSource::Previous;
  slot.operation = ChainOperation::Next;
  slot.out_channel = 0;
  snapshots_[index] = {};
  snapshot_initialized_[index] = false;
  return true;
}
bool Chain::configure(std::uint8_t index, ChainSource source, ChainOperation operation, std::uint8_t channel) noexcept {
  if (index >= kSlots || static_cast<std::uint8_t>(source) > static_cast<std::uint8_t>(ChainSource::Slot5) ||
      static_cast<std::uint8_t>(operation) > static_cast<std::uint8_t>(ChainOperation::Skip) || channel > kMidiChannelMax ||
      static_cast<std::uint8_t>(source) >= index + 2U) {
    ++diagnostics_.invalid_config;
    return false;
  }
  auto& slot = slots_[index]; slot.source = source; slot.operation = operation; slot.out_channel = channel;
  return true;
}
void Chain::process(const ProcessContext& context, const NoteBatch& input, OutputCollector& output) noexcept {
  if (recovery_required_) { output.fault(); final_stream_ = {}; return; }
  // Any refused append anywhere (input slot or module) may have lost a release.
  // Never continue silently: count it and force the exact recovery
  // (Output::all_notes_off via the sounding bitmap) when the collector drains.
  const auto batch_fault = [&]() noexcept {
    ++diagnostics_.batch_overflow;
    recovery_required_ = true;
    output.fault();
    final_stream_ = {};
  };
  if (input.overflowed()) { batch_fault(); return; }
  NoteBatch stream = input;
  for (std::uint8_t i = 0; i < kSlots; ++i) {
    auto& slot = slots_[i];
    const NoteBatch* source = &stream;
    if (slot.source == ChainSource::Input) source = &input;
    else if (slot.source != ChainSource::Previous) {
      const auto tap = static_cast<std::uint8_t>(slot.source) - 2U;
      if (tap >= i) { ++diagnostics_.invalid_config; continue; }
      if (snapshot_initialized_[tap]) source = &snapshots_[tap];
    }
    const NoteBatch selected = *source;  // Per-pass immutable tap snapshot.
    if (slot.operation == ChainOperation::Skip) {
      stream = selected;
      continue;
    }
    NoteBatch processed{};
    slot.descriptor->process(slot.state, context, selected, processed);
    if (processed.overflowed()) { batch_fault(); return; }
    NoteBatch combined = processed;
    if (slot.operation == ChainOperation::Add) {
      for (std::uint16_t n = 0; n < selected.off.count; ++n)
        (void)combined.off.append(selected.off.items[n]);
      for (std::uint16_t n = 0; n < selected.on.count; ++n)
        (void)combined.on.append(selected.on.items[n]);
      if (combined.overflowed()) { ++diagnostics_.scratch_overflow; batch_fault(); return; }
    }
    snapshots_[i] = combined;
    snapshot_initialized_[i] = true;
    if (slot.operation == ChainOperation::OutNext || slot.operation == ChainOperation::OutSource) {
      if (!output.append(processed, slot.out_channel)) {
        ++diagnostics_.output_overflow;
        recovery_required_ = true;
        final_stream_ = {};
        return;
      }
    }
    stream = slot.operation == ChainOperation::OutSource ? selected : combined;
    if (recovery_required_) { final_stream_ = {}; return; }
  }
  final_stream_ = stream;
}
bool Chain::swap_slots(std::uint8_t a, std::uint8_t b) noexcept {
  if (a >= kSlots || b >= kSlots) return false;
  if (a == b) return true;
  // Bytewise exchange: a ModuleState temporary would exceed the core stack budget.
  auto* pa = reinterpret_cast<unsigned char*>(&slots_[a]);
  auto* pb = reinterpret_cast<unsigned char*>(&slots_[b]);
  for (std::size_t i = 0; i < sizeof(Slot); ++i) {
    const unsigned char t = pa[i]; pa[i] = pb[i]; pb[i] = t;
  }
  snapshots_[a] = {}; snapshots_[b] = {};
  snapshot_initialized_[a] = false; snapshot_initialized_[b] = false;
  return true;
}
void Chain::reset(ResetReason reason) noexcept {
  for (auto& slot : slots_) slot.descriptor->reset(slot.state, reason);
  for (auto& snapshot : snapshots_) snapshot = {};
  for (auto& initialized : snapshot_initialized_) initialized = false;
  final_stream_ = {};
  recovery_required_ = false;
}
}  // namespace orbit
