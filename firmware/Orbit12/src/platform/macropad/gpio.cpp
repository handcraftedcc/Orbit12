#include "gpio.hpp"

#include <Arduino.h>

namespace orbit {
namespace platform {
namespace {
// MacroPad keys are GPIO 1..12 (Adafruit pinout), active low.
constexpr std::uint8_t kFirstKeyPin = 1;

std::uint16_t read_keys() noexcept {
  std::uint16_t mask = 0;
  for (std::uint8_t key = 0; key < kKeys; ++key) {
    if (digitalRead(kFirstKeyPin + key) == LOW) mask = static_cast<std::uint16_t>(mask | (1U << key));
  }
  return mask;
}
bool read_switch() noexcept { return digitalRead(PIN_SWITCH) == LOW; }
TimeSample clock_sample(void*) { return sample_time(); }
bool input_poll(void* ctx, InputFrame& frame) { return static_cast<GpioInput*>(ctx)->poll_frame(frame); }
}  // namespace

TimeSample sample_time() noexcept {
  const std::uint64_t us = time_us_64();
  return {static_cast<std::uint32_t>(us / 1000U), static_cast<std::uint32_t>(us)};
}

ClockPort clock_port() noexcept { return {nullptr, &clock_sample}; }

void GpioInput::begin(bool use_encoder_interrupt) noexcept {
  for (std::uint8_t key = 0; key < kKeys; ++key) pinMode(kFirstKeyPin + key, INPUT_PULLUP);
  pinMode(PIN_SWITCH, INPUT_PULLUP);
  const std::uint32_t now = sample_time().ms;
  key_state_ = read_keys();
  switch_state_ = read_switch();
  keys_.reset(key_state_, now);
  switch_.reset(switch_state_ ? 1U : 0U, now);
  last_key_ms_ = last_switch_ms_ = now;
  encoder_.begin(use_encoder_interrupt);
}

bool GpioInput::scan(GpioSample& out) noexcept {
  out.edge_count = 0;
  out.switch_pressed = out.switch_released = false;
  const std::uint32_t now = sample_time().ms;
  encoder_.poll();
  bool any = false;
  if (elapsed(now, last_key_ms_) >= kKeyScanMs) {
    last_key_ms_ = now;
    const std::uint16_t changed = keys_.update(read_keys(), now);
    key_state_ = keys_.stable();
    for (std::uint8_t key = 0; key < kKeys; ++key) {
      if (changed & (1U << key)) {
        out.edges[out.edge_count++] = {key, (key_state_ & (1U << key)) != 0, now};
        any = true;
      }
    }
  }
  if (elapsed(now, last_switch_ms_) >= kSwitchScanMs) {
    last_switch_ms_ = now;
    if (switch_.update(read_switch() ? 1U : 0U, now)) {
      const bool down = (switch_.stable() & 1U) != 0;
      out.switch_pressed = down && !switch_state_;
      out.switch_released = !down && switch_state_;
      switch_state_ = down;
      any = true;
    }
  }
  out.key_mask = key_state_;
  out.switch_down = switch_state_;
  return any;
}

bool GpioInput::poll_frame(InputFrame& frame) noexcept {
  GpioSample sample;
  bool any = scan(sample);
  for (std::uint8_t i = 0; i < sample.edge_count; ++i) frame.keys.append(sample.edges[i]);
  const std::int16_t delta = take_encoder_delta();
  if (delta != 0) {
    const std::int32_t sum = static_cast<std::int32_t>(frame.encoder_delta) + delta;
    frame.encoder_delta = static_cast<std::int16_t>(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
    any = true;
  }
  frame.encoder_down = sample.switch_down;
  frame.encoder_pressed = frame.encoder_pressed || sample.switch_pressed;
  frame.encoder_released = frame.encoder_released || sample.switch_released;
  return any;
}

InputPort GpioInput::input_port() noexcept { return {this, &input_poll}; }

}  // namespace platform
}  // namespace orbit
