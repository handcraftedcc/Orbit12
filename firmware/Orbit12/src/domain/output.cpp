#include "output.hpp"

namespace orbit {
namespace {
inline std::uint16_t bit_index(std::uint8_t channel, std::uint8_t pitch) noexcept {
  return static_cast<std::uint16_t>(channel * 128U + pitch);
}
}  // namespace

void Output::set_sounding(std::uint8_t ch, std::uint8_t pitch, bool value) noexcept {
  const std::uint16_t bit = bit_index(ch, pitch);
  const std::uint32_t mask = 1UL << (bit & 31U);
  std::uint32_t& word = sounding_[bit >> 5];
  const bool was = (word & mask) != 0;
  if (was == value) return;
  if (value) { word |= mask; ++sounding_count_; }
  else { word &= ~mask; --sounding_count_; }
}
void Output::set_owed(std::uint8_t ch, std::uint8_t pitch, bool value) noexcept {
  const std::uint16_t bit = bit_index(ch, pitch);
  const std::uint32_t mask = 1UL << (bit & 31U);
  std::uint32_t& word = off_owed_[bit >> 5];
  const bool was = (word & mask) != 0;
  if (was == value) return;
  if (value) { word |= mask; ++owed_count_; }
  else { word &= ~mask; --owed_count_; }
}
bool Output::push(const MidiPacket& packet) noexcept {
  if (count_ >= kQueueCapacity) return false;
  queue_[count_++] = packet;
  if (packet.kind == MidiKind::Clock) { ++clock_runs_; clocks_ = static_cast<std::uint16_t>(clocks_ + packet.data1); }
  return true;
}
void Output::erase(std::uint16_t index) noexcept {
  if (index >= count_) return;
  if (queue_[index].kind == MidiKind::Clock) { --clock_runs_; clocks_ = static_cast<std::uint16_t>(clocks_ - queue_[index].data1); }
  for (std::uint16_t i = static_cast<std::uint16_t>(index + 1); i < count_; ++i) queue_[i - 1] = queue_[i];
  --count_;
}
bool Output::queued(MidiKind kind, std::uint8_t pitch, std::uint8_t channel) const noexcept {
  for (std::uint16_t i = 0; i < count_; ++i)
    if (queue_[i].kind == kind && queue_[i].data1 == pitch && queue_[i].channel == channel) return true;
  return false;
}
void Output::drop_pending_ons(std::uint8_t pitch, std::uint8_t channel) noexcept {
  for (std::uint16_t i = count_; i > 0; --i) {
    const auto& e = queue_[i - 1];
    if (e.kind == MidiKind::NoteOn && e.data1 == pitch && e.channel == channel) erase(static_cast<std::uint16_t>(i - 1));
  }
}
void Output::owe_all_sounding(std::int16_t channel) noexcept {
  for (std::uint16_t w = 0; w < kWords; ++w) {
    std::uint32_t bits = sounding_[w];
    for (std::uint8_t b = 0; bits; ++b, bits >>= 1) {
      if (!(bits & 1U)) continue;
      const std::uint16_t idx = static_cast<std::uint16_t>(w * 32U + b);
      const std::uint8_t ch = static_cast<std::uint8_t>(idx >> 7);
      if (channel >= 0 && ch != channel) continue;
      set_owed(ch, static_cast<std::uint8_t>(idx & 127U), true);
    }
  }
}
void Output::begin_recovery() noexcept {
  if (recovery_) return;
  recovery_ = true;
  restart_required_ = true;
  ++diagnostics_.recoveries;
  // Discard everything unsent (ons, clocks, queued offs); every accepted-on
  // identity owes exactly one release, then a Stop.
  count_ = 0; clock_runs_ = 0; clocks_ = 0;
  owe_all_sounding(-1);
  stop_owed_ = true;
}
void Output::request_off(std::uint8_t pitch, std::uint8_t channel) noexcept {
  // A pending (unsent) press followed by a release cancels the press.
  drop_pending_ons(pitch, channel);
  if (!test(sounding_, channel, pitch)) return;           // never sounded: nothing to release
  if (test(off_owed_, channel, pitch)) return;            // release already owed
  if (queued(MidiKind::NoteOff, pitch, channel)) return;  // exact duplicate
  if (!push({MidiKind::NoteOff, channel, pitch, 0})) {
    set_owed(channel, pitch, true);  // FIFO full: never drop a release
    ++diagnostics_.owed_deferred;
  }
}
bool Output::request_on(std::uint8_t pitch, std::uint8_t velocity, std::uint8_t channel) noexcept {
  for (std::uint16_t i = count_; i > 0; --i) {
    const auto& e = queue_[i - 1];
    if ((e.kind != MidiKind::NoteOn && e.kind != MidiKind::NoteOff) || e.data1 != pitch || e.channel != channel) continue;
    if (e.kind == MidiKind::NoteOn) return true;  // exact duplicate, no opposite event after it
    break;
  }
  if (!push({MidiKind::NoteOn, channel, pitch, velocity})) { ++diagnostics_.queue_overflow; return false; }
  return true;
}
bool Output::submit(const NoteBatch& batch, std::uint8_t channel) noexcept {
  if (channel > kMidiChannelMax || recovery_ || !connected_) return false;
  bool accepted = true;
  for (std::uint16_t i = 0; i < batch.off.count; ++i) {
    const auto& n = batch.off.items[i];
    if (!n.valid()) { accepted = false; continue; }
    request_off(n.pitch, channel);
  }
  for (std::uint16_t i = 0; i < batch.on.count; ++i) {
    const auto& n = batch.on.items[i];
    if (!n.valid()) { accepted = false; continue; }
    if (!request_on(n.pitch, n.velocity, channel)) accepted = false;
  }
  return accepted;
}
bool Output::all_notes_off(std::int16_t channel) noexcept {
  if (channel < -1 || channel > kMidiChannelMax) return false;
  // Unsent ons never reached the instrument; queued offs are superseded by
  // owed offs (which drain first), so no release is sent twice.
  for (std::uint16_t i = count_; i > 0; --i) {
    const auto& e = queue_[i - 1];
    if ((e.kind == MidiKind::NoteOn || e.kind == MidiKind::NoteOff) && (channel < 0 || e.channel == channel))
      erase(static_cast<std::uint16_t>(i - 1));
  }
  owe_all_sounding(channel);
  return true;
}
bool Output::queue_clock(std::uint16_t ticks) noexcept {
  if (recovery_ || !connected_) return false;
  if (ticks == 0) return true;
  if (ticks > kClockBacklog || clocks_ + ticks > kClockBacklog) {
    ++diagnostics_.queue_overflow;
    begin_recovery();
    return false;
  }
  if (count_ && queue_[count_ - 1].kind == MidiKind::Clock) {
    queue_[count_ - 1].data1 = static_cast<std::uint8_t>(queue_[count_ - 1].data1 + ticks);  // total <= 96
    clocks_ = static_cast<std::uint16_t>(clocks_ + ticks);
    return true;
  }
  if (!push({MidiKind::Clock, 0, static_cast<std::uint8_t>(ticks), 0})) {
    ++diagnostics_.queue_overflow;
    begin_recovery();
    return false;
  }
  return true;
}
bool Output::queue_start() noexcept {
  if (recovery_ || !connected_) return false;
  if (push({MidiKind::Start, 0, 0, 0})) return true;
  ++diagnostics_.queue_overflow;
  return false;
}
bool Output::queue_stop() noexcept {
  if (!connected_) return false;
  if (!push({MidiKind::Stop, 0, 0, 0})) stop_owed_ = true;  // Stop is never dropped
  return true;
}
std::uint16_t Output::flush(const MidiPort& port, std::uint16_t limit) noexcept {
  if (!connected_) return 0;
  if (limit > kFlushLimit) limit = kFlushLimit;
  std::uint16_t sent = 0;
  // 1. Owed releases first: they predate anything still in the FIFO that could
  //    re-press the same key. Stop at the first refusal to keep that order.
  if (owed_count_) {
    for (std::uint16_t w = 0; w < kWords; ++w) {
      while (off_owed_[w]) {
        if (sent >= limit) return sent;
        std::uint8_t b = 0;
        while (!((off_owed_[w] >> b) & 1U)) ++b;
        const std::uint16_t idx = static_cast<std::uint16_t>(w * 32U + b);
        const std::uint8_t ch = static_cast<std::uint8_t>(idx >> 7), pitch = static_cast<std::uint8_t>(idx & 127U);
        if (!port.send({MidiKind::NoteOff, ch, pitch, 0})) { ++diagnostics_.refused; return sent; }
        set_owed(ch, pitch, false);
        set_sounding(ch, pitch, false);
        ++sent;
      }
    }
  }
  if (stop_owed_) {
    if (sent >= limit) return sent;
    if (!port.send({MidiKind::Stop, 0, 0, 0})) { ++diagnostics_.refused; return sent; }
    stop_owed_ = false;
    ++sent;
  }
  // 2. FIFO strictly in arrival order.
  while (count_ && sent < limit) {
    const MidiPacket head = queue_[0];
    MidiPacket packet = head;
    if (head.kind == MidiKind::Clock) packet = {MidiKind::Clock, 0, 0, 0};
    if (!port.send(packet)) { ++diagnostics_.refused; break; }
    if (head.kind == MidiKind::Clock && head.data1 > 1) {
      --queue_[0].data1;
      --clocks_;
    } else {
      erase(0);
    }
    if (head.kind == MidiKind::NoteOn) set_sounding(head.channel, head.data1, true);
    else if (head.kind == MidiKind::NoteOff) set_sounding(head.channel, head.data1, false);
    ++sent;
  }
  return sent;
}
void Output::set_connected(bool connected) noexcept {
  if (connected_ == connected) return;
  connected_ = connected;
  count_ = 0; clock_runs_ = 0; clocks_ = 0;  // stale ons/clocks/offs; sounding bits keep the truth
  recovery_ = true;
  restart_required_ = true;
  if (!connected) { ++diagnostics_.disconnected; return; }
  owe_all_sounding(-1);
  stop_owed_ = true;
}
bool Output::request_restart() noexcept {
  if (!connected_ || count_ != 0 || owed_count_ != 0 || sounding_count_ != 0 || stop_owed_) return false;
  recovery_ = false;
  restart_required_ = false;
  return true;
}
}  // namespace orbit
