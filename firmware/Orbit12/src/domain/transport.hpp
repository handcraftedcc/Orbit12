#pragma once

#include <cstddef>
#include <cstdint>

#include "types.hpp"

namespace orbit {

enum class RealtimeKind : std::uint8_t { Clock, Start, Continue, Stop };
struct RealtimeEvent { RealtimeKind kind = RealtimeKind::Clock; };
using RealtimeEvents = FixedBuffer<RealtimeEvent, 2>;
enum class MidiInputKind : std::uint8_t { Other, Clock, Start, Continue, Stop };
struct MidiInput { MidiInputKind kind = MidiInputKind::Other; };

// The caller retains the receive queue and advances by consumed_messages.
// At most 200 messages are inspected and at most one TimingClock is consumed.
struct TransportUpdate {
  TransportView view{};
  RealtimeEvents output{};
  std::uint16_t consumed_messages = 0;
};

class Transport {
 public:
  explicit Transport(const GlobalState& state, std::uint32_t now_us = 0) noexcept;
  bool set_bpm(std::uint16_t bpm) noexcept;
  bool set_swing(float swing) noexcept;
  void set_mode(TransportMode mode, std::uint32_t now_us) noexcept;
  void reset(std::uint32_t now_us) noexcept;
  void start(std::uint32_t now_us, RealtimeEvents* output = nullptr) noexcept;
  void stop(std::uint32_t now_us, RealtimeEvents* output = nullptr) noexcept;
  void continue_transport() noexcept;
  TransportUpdate update(TimeSample now, const MidiInput* input,
                         std::size_t input_count) noexcept;

  bool running() const noexcept { return running_; }
  std::uint32_t tick() const noexcept { return tick_; }
  std::uint32_t next_us() const noexcept { return next_us_; }

 private:
  void schedule_next() noexcept;
  void append_event(RealtimeEvents* output, RealtimeKind kind) const noexcept;

  std::uint16_t bpm_ = 120;
  float swing_ = 0.0f;
  TransportMode mode_ = TransportMode::Internal;
  bool running_ = false;
  std::uint32_t tick_ = 1;
  std::uint32_t next_us_ = 0;
  double tick_error_us_ = 0.0;
};

}  // namespace orbit
