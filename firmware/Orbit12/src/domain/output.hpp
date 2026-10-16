#pragma once

#include "types.hpp"

namespace orbit {

enum class MidiKind : std::uint8_t { NoteOff, NoteOn, Clock, Start, Stop, Continue };
struct MidiPacket {
  MidiKind kind = MidiKind::Clock;
  std::uint8_t channel = 0;
  std::uint8_t data1 = 0;
  std::uint8_t data2 = 0;
};
struct MidiPort {
  void* context = nullptr;
  bool (*try_send)(void*, const MidiPacket&) = nullptr;
  bool send(const MidiPacket& packet) const noexcept { return try_send && try_send(context, packet); }
};
struct OutputDiagnostics {
  std::uint32_t refused = 0;         // Port refused a write (packet or owed off retained).
  std::uint32_t queue_overflow = 0;  // Note-on / clock / start that did not fit the FIFO and was refused.
  std::uint32_t owed_deferred = 0;   // Note-offs that did not fit the FIFO and became owed (never dropped).
  std::uint32_t recoveries = 0;
  std::uint32_t disconnected = 0;
};

// Single ordered FIFO of events, flushed strictly in arrival order, plus two
// per-(channel,pitch) bitmaps:
//   sounding : note-on accepted by the port and no note-off accepted since.
//   off_owed : note-off requested but not yet accepted (FIFO full or port
//              refused). Owed offs drain before the FIFO, so a release is
//              never lost and never overtaken by a later press of its key.
// Consecutive Clock ticks are run-length merged into one FIFO entry (data1).
class Output {
 public:
  static constexpr std::uint16_t kQueueCapacity = 48;
  static constexpr std::uint16_t kFlushLimit = 32;
  static constexpr std::uint8_t kClockBacklog = 96;

  bool submit(const NoteBatch& batch, std::uint8_t channel = 0) noexcept;
  bool all_notes_off(std::int16_t channel = -1) noexcept;
  bool queue_clock(std::uint16_t ticks = 1) noexcept;
  bool queue_start() noexcept;
  bool queue_stop() noexcept;
  std::uint16_t flush(const MidiPort& port, std::uint16_t limit = kFlushLimit) noexcept;
  void set_connected(bool connected) noexcept;
  bool request_restart() noexcept;
  bool recovering() const noexcept { return recovery_; }
  bool connected() const noexcept { return connected_; }
  // Outstanding packets: FIFO entries (clock runs expanded) + owed offs + owed stop.
  std::uint16_t pending() const noexcept {
    return static_cast<std::uint16_t>(count_ - clock_runs_ + clocks_ + owed_count_ + (stop_owed_ ? 1U : 0U));
  }
  // Number of (channel,pitch) identities currently sounding.
  std::uint16_t held_count() const noexcept { return sounding_count_; }
  std::uint16_t owed_count() const noexcept { return owed_count_; }
  bool is_sounding(std::uint8_t channel, std::uint8_t pitch) const noexcept {
    return channel <= kMidiChannelMax && pitch <= kMidiMax && test(sounding_, channel, pitch);
  }
  bool is_off_owed(std::uint8_t channel, std::uint8_t pitch) const noexcept {
    return channel <= kMidiChannelMax && pitch <= kMidiMax && test(off_owed_, channel, pitch);
  }
  const OutputDiagnostics& diagnostics() const noexcept { return diagnostics_; }

 private:
  static constexpr std::uint16_t kWords = 16 * 128 / 32;
  MidiPacket queue_[kQueueCapacity]{};
  std::uint32_t sounding_[kWords]{};
  std::uint32_t off_owed_[kWords]{};
  std::uint16_t count_ = 0;
  std::uint16_t clock_runs_ = 0;  // FIFO entries of kind Clock
  std::uint16_t clocks_ = 0;      // total pending clock ticks
  std::uint16_t sounding_count_ = 0;
  std::uint16_t owed_count_ = 0;
  bool connected_ = true;
  bool recovery_ = false;
  bool restart_required_ = false;
  bool stop_owed_ = false;
  OutputDiagnostics diagnostics_{};

  static bool test(const std::uint32_t* map, std::uint8_t ch, std::uint8_t pitch) noexcept {
    const std::uint16_t bit = static_cast<std::uint16_t>(ch * 128U + pitch);
    return (map[bit >> 5] >> (bit & 31U)) & 1U;
  }
  void set_sounding(std::uint8_t ch, std::uint8_t pitch, bool value) noexcept;
  void set_owed(std::uint8_t ch, std::uint8_t pitch, bool value) noexcept;
  bool push(const MidiPacket& packet) noexcept;
  void erase(std::uint16_t index) noexcept;
  bool queued(MidiKind kind, std::uint8_t pitch, std::uint8_t channel) const noexcept;
  void drop_pending_ons(std::uint8_t pitch, std::uint8_t channel) noexcept;
  void request_off(std::uint8_t pitch, std::uint8_t channel) noexcept;
  bool request_on(std::uint8_t pitch, std::uint8_t velocity, std::uint8_t channel) noexcept;
  void owe_all_sounding(std::int16_t channel) noexcept;
  void begin_recovery() noexcept;
};
}  // namespace orbit
