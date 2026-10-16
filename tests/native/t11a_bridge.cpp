#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "app/bridge.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

struct Payload { std::uint32_t value = 0; };

void queue_boundaries_and_wrap() {
  orbit::SpscQueue<Payload, 3> queue(0xfffffffeU);
  CHECK(queue.try_push({1}));
  CHECK(queue.try_push({2}));
  CHECK(queue.try_push({3}));
  CHECK(!queue.try_push({4}));
  Payload item{};
  CHECK(queue.try_pop(item) && item.value == 1);
  CHECK(queue.try_pop(item) && item.value == 2);
  CHECK(queue.try_push({4}));  // Counter has crossed uint32 wrap.
  CHECK(queue.try_pop(item) && item.value == 3);
  CHECK(queue.try_pop(item) && item.value == 4);
  CHECK(!queue.try_pop(item));
}

void snapshot_latest_and_stall() {
  orbit::UiSnapshotQueue<Payload> queue;
  CHECK(queue.try_push({1, 7, {10}}));
  CHECK(queue.try_push({2, 7, {20}}));
  CHECK(queue.try_push({3, 7, {30}}));
  CHECK(!queue.try_push({4, 7, {40}}));
  orbit::UiSnapshot<Payload> newest{};
  CHECK(queue.drain_newest(newest) == 3U);
  CHECK(newest.revision == 3 && newest.epoch == 7 && newest.model.value == 30);
}

void physical_overflow_epoch_and_resync() {
  orbit::PhysicalEventBridge bridge;
  for (std::uint32_t i = 0; i < 64; ++i) {
    orbit::PhysicalEvent event{};
    event.kind = orbit::InputKind::EncoderDelta;
    event.at_ms = i;
    CHECK(bridge.publish(event, 0x5U));
  }
  orbit::PhysicalEvent overflow{};
  overflow.kind = orbit::InputKind::KeyEdge;
  CHECK(!bridge.publish(overflow, 0x7U));
  CHECK(bridge.faulted());
  const auto physical = bridge.state();
  CHECK(physical.key_mask == 0x7U && physical.fault_generation == 1U);
  bridge.acknowledge_fault(physical.fault_generation);
  CHECK(!bridge.faulted());
  CHECK(bridge.awaiting_resync());

  orbit::PhysicalEvent first{};
  CHECK(bridge.try_pop(first));
  CHECK(first.sequence == 0U && first.epoch == 0U);
  CHECK(!bridge.accept_for_musical_input(first));  // Overflow gates keys until resync.
  CHECK(bridge.awaiting_resync());
  CHECK(bridge.change_epoch(9U) == 63U);
  CHECK(!bridge.accept_for_musical_input(first));
  orbit::PhysicalEvent resync{};
  resync.kind = orbit::InputKind::Resync;
  CHECK(bridge.publish(resync, 0));
  // Only the new epoch's authoritative release remains queued and is accepted.
  orbit::PhysicalEvent event{};
  const bool popped = bridge.try_pop(event);
  CHECK(popped && event.epoch == 9U);
  const bool accepted_resync = popped && bridge.accept_for_musical_input(event);
  CHECK(accepted_resync && !bridge.awaiting_resync());
}

void startup_and_maintenance() {
  orbit::StartupFlags ready;
  CHECK(!ready.all_ready());
  ready.core0_ready();
  CHECK(ready.core0_is_ready() && !ready.all_ready());
  ready.core1_ready();
  CHECK(ready.all_ready());

  orbit::MaintenanceHandshake handshake;
  const auto seq = handshake.request(0xfffffff0U, 32U);
  CHECK(!handshake.acknowledged(seq));
  CHECK(!handshake.timed_out(0x0000000fU));
  CHECK(handshake.worker_poll() == orbit::WorkerAction::Quiesce);
  CHECK(!handshake.acknowledged(seq));  // ACK follows the bounded transfer.
  handshake.worker_acknowledge_quiescence();
  CHECK(handshake.acknowledged(seq));
  handshake.resume(seq);
  CHECK(handshake.worker_poll() == orbit::WorkerAction::Resume);

  const auto timed_seq = handshake.request(100U, 5U);
  CHECK(handshake.timed_out(105U));
  handshake.cancel();
  CHECK(handshake.worker_poll() == orbit::WorkerAction::None);
  CHECK(timed_seq != seq);
}

void threaded_stress() {
  constexpr std::uint32_t count = 200000U;
  orbit::SpscQueue<Payload, 64> queue;
  std::atomic<bool> failed{false};
  std::thread producer([&] {
    for (std::uint32_t i = 0; i < count; ++i) {
      while (!queue.try_push(Payload{i})) {}
    }
  });
  std::thread consumer([&] {
    Payload item{};
    for (std::uint32_t i = 0; i < count; ++i) {
      while (!queue.try_pop(item)) {}
      if (item.value != i) failed.store(true, std::memory_order_relaxed);
    }
  });
  producer.join();
  consumer.join();
  CHECK(!failed.load());
}

}  // namespace

int main() {
  queue_boundaries_and_wrap();
  snapshot_latest_and_stall();
  physical_overflow_epoch_and_resync();
  startup_and_maintenance();
  threaded_stress();
  if (failures) return 1;
  std::puts("T11a bridge checks passed");
  return 0;
}
