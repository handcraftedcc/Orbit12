#include "domain/transport.hpp"

#include "domain/time.hpp"

namespace orbit {

Transport::Transport(const GlobalState& state, std::uint32_t now_us) noexcept
    : bpm_(state.bpm), swing_(state.swing), mode_(state.transport_mode), next_us_(now_us) {
  if (bpm_ < 1 || bpm_ > 300) bpm_ = 120;
  if (!(swing_ >= 0.0f && swing_ <= 1.0f)) swing_ = 0.0f;
}

bool Transport::set_bpm(std::uint16_t bpm) noexcept {
  if (bpm < 1 || bpm > 300) return false;
  bpm_ = bpm;
  tick_error_us_ = 0.0;
  return true;
}

bool Transport::set_swing(float swing) noexcept {
  if (!(swing >= 0.0f && swing <= 1.0f)) return false;
  swing_ = swing;
  return true;
}

void Transport::set_mode(TransportMode mode, std::uint32_t now_us) noexcept {
  mode_ = mode;
  running_ = false;
  reset(now_us);
}

void Transport::reset(std::uint32_t now_us) noexcept {
  tick_ = 0;
  tick_error_us_ = 0.0;
  next_us_ = now_us;
}

void Transport::append_event(RealtimeEvents* output, RealtimeKind kind) const noexcept {
  if (output != nullptr) output->append(RealtimeEvent{kind});
}

void Transport::start(std::uint32_t now_us, RealtimeEvents* output) noexcept {
  reset(now_us);
  schedule_next();
  running_ = true;
  append_event(output, RealtimeKind::Start);
  append_event(output, RealtimeKind::Clock);
}

void Transport::stop(std::uint32_t now_us, RealtimeEvents* output) noexcept {
  reset(now_us);
  running_ = false;
  append_event(output, RealtimeKind::Stop);
}

void Transport::continue_transport() noexcept {
  // CircuitPython receives Continue but intentionally leaves transport state unchanged.
}

void Transport::schedule_next() noexcept {
  const std::uint32_t next_tick = tick_ + 1U;
  float factor = 1.0f;
  if (swing_ != 0.0f) {
    const auto pair_position = next_tick % 12U;
    factor = pair_position > 0U && pair_position <= 6U
                 ? 1.0f + swing_ * 0.6f
                 : 1.0f - swing_ * 0.6f;
  }
  const double exact = (60000000.0 / (static_cast<double>(bpm_) * 24.0)) * factor;
  const auto whole = static_cast<std::uint32_t>(exact);
  tick_error_us_ += exact - static_cast<double>(whole);
  std::uint32_t interval = whole;
  if (tick_error_us_ >= 1.0) {
    ++interval;
    tick_error_us_ -= 1.0;
  }
  next_us_ += interval;
}

TransportUpdate Transport::update(TimeSample now, const MidiInput* input,
                                  std::size_t input_count) noexcept {
  TransportUpdate result{};
  const std::size_t available = input == nullptr ? 0 : input_count;
  const std::size_t limit = available < 200U ? available : 200U;
  if (mode_ == TransportMode::External) {
    for (std::size_t i = 0; i < limit; ++i) {
      ++result.consumed_messages;
      switch (input[i].kind) {
        case MidiInputKind::Clock:
          if (!running_) {
            reset(now.us);
            schedule_next();
            running_ = true;
          }
          ++tick_;
          result.view.ticked = true;
          i = limit;  // Source consumes no more than one TimingClock per pass.
          break;
        case MidiInputKind::Start:
          reset(now.us);
          running_ = false;
          result.view.ticked = false;
          break;
        case MidiInputKind::Stop:
          stop(now.us);
          result.view.ticked = false;
          break;
        case MidiInputKind::Continue:
        case MidiInputKind::Other:
          break;
      }
      if (result.view.ticked) break;
    }
  } else {
    result.consumed_messages = static_cast<std::uint16_t>(limit);
    // CircuitPython uses ticks_less(scheduled, now), a strict comparison.
    if (running_ && deadline_reached(now.us, next_us_ + 1U)) {
      schedule_next();
      ++tick_;
      result.view.ticked = true;
      result.output.append(RealtimeEvent{RealtimeKind::Clock});
    }
  }
  result.view.running = running_;
  result.view.tick = tick_;
  result.view.next_us = next_us_;
  return result;
}

}  // namespace orbit
