#include <cstdio>
#include <initializer_list>
#include <limits>

#include "domain/transport.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

// Golden expectations are manually derived from
// circuitpython-source/src/apps/Orion/core/transport.py:
// next_tick_swing_factor(), schedule_next_tick(), clock_start(), clock_stop(), update().
void internal_clock_fixtures() {
  for (const auto bpm : {1U, 120U, 300U}) {
    for (const auto swing : {0.0f, 1.0f}) {
      orbit::GlobalState state{};
      state.bpm = static_cast<std::uint16_t>(bpm);
      state.swing = swing;
      orbit::Transport transport(state, 100000U);
      orbit::RealtimeEvents start_events;
      transport.start(100000U, &start_events);
      const double nominal = 60000000.0 / (bpm * 24.0);
      const auto first_factor = swing == 0.0f ? 1.0f : 1.0f + swing * 0.6f;
      CHECK(start_events.count == 2);
      CHECK(start_events.items[0].kind == orbit::RealtimeKind::Start);
      CHECK(start_events.items[1].kind == orbit::RealtimeKind::Clock);
      CHECK(transport.tick() == 0U);
      CHECK(transport.next_us() == 100000U + static_cast<std::uint32_t>(nominal * first_factor));
      auto early = transport.update({0, transport.next_us()}, nullptr, 0);
      CHECK(!early.view.ticked && early.view.tick == 0U);
      auto due = transport.update({0, transport.next_us() + 1U}, nullptr, 0);
      CHECK(due.view.ticked && due.view.tick == 1U);
      CHECK(due.output.count == 1 && due.output.items[0].kind == orbit::RealtimeKind::Clock);
      // A very late poll advances one tick only; following polls catch up.
      auto late = transport.update({0, due.view.next_us + 5000000U}, nullptr, 0);
      CHECK(late.view.ticked && late.view.tick == 2U);
      auto late_again = transport.update({0, due.view.next_us + 5000000U}, nullptr, 0);
      CHECK(late_again.view.ticked && late_again.view.tick == 3U);
      orbit::RealtimeEvents stop_events;
      transport.stop(0xfffffff0U, &stop_events);
      CHECK(!transport.running() && transport.tick() == 0U);
      CHECK(stop_events.count == 1 && stop_events.items[0].kind == orbit::RealtimeKind::Stop);
    }
  }
}

void wrap_and_swing_pair() {
  orbit::GlobalState state{};
  state.bpm = 120;
  state.swing = 1.0f;
  orbit::Transport transport(state, 0xfffffff0U);
  transport.start(0xfffffff0U);
  const auto first_due = transport.next_us();
  CHECK(first_due < 0xfffffff0U);  // Microsecond deadline wraps.
  auto first = transport.update({0, first_due + 1U}, nullptr, 0);
  CHECK(first.view.ticked && first.view.tick == 1U);
  const auto second_interval = first.view.next_us - first_due;
  CHECK(second_interval > static_cast<std::uint32_t>(60000000U / (120U * 24U)));
  for (int i = 0; i < 5; ++i) {
    auto tick = transport.update({0, transport.next_us() + 1U}, nullptr, 0);
    CHECK(tick.view.ticked);
  }
  const auto seventh_due = transport.next_us();
  auto seventh = transport.update({0, seventh_due + 1U}, nullptr, 0);
  CHECK(seventh.view.tick == 7U);
  CHECK(seventh.view.next_us - seventh_due <
        static_cast<std::uint32_t>(60000000U / (120U * 24U)));
}

void external_queue_contract() {
  orbit::GlobalState state{};
  state.transport_mode = orbit::TransportMode::External;
  orbit::Transport transport(state, 0xfffffff0U);
  orbit::MidiInput queue[205]{};
  for (auto& message : queue) message.kind = orbit::MidiInputKind::Other;
  queue[199].kind = orbit::MidiInputKind::Clock;
  queue[200].kind = orbit::MidiInputKind::Clock;
  auto first = transport.update({0, 10U}, queue, 205);
  CHECK(first.consumed_messages == 200U);
  CHECK(first.view.running && first.view.ticked && first.view.tick == 1U);
  CHECK(first.output.count == 0);
  auto second = transport.update({0, 20U}, queue + 200, 5);
  CHECK(second.consumed_messages == 1U);
  CHECK(second.view.ticked && second.view.tick == 2U);

  orbit::MidiInput controls[] = {{orbit::MidiInputKind::Start},
                                 {orbit::MidiInputKind::Continue},
                                 {orbit::MidiInputKind::Clock}};
  auto control = transport.update({0, 30U}, controls, 3);
  CHECK(control.view.running && control.view.tick == 1U);
  CHECK(control.consumed_messages == 3U);
  orbit::MidiInput stop[] = {{orbit::MidiInputKind::Stop}};
  auto stopped = transport.update({0, 40U}, stop, 1);
  CHECK(!stopped.view.running && stopped.view.tick == 0U);
  CHECK(stopped.consumed_messages == 1U);
  transport.continue_transport();
  CHECK(!transport.running());  // Source Continue input does not restart.
}

}  // namespace

int main() {
  internal_clock_fixtures();
  wrap_and_swing_pair();
  external_queue_contract();
  if (failures) return 1;
  std::puts("T04 transport checks passed");
  return 0;
}
