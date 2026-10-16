#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "domain/time.hpp"

namespace orbit {

enum class InputKind : std::uint8_t { KeyEdge, EncoderSwitch, EncoderDelta, Resync };
struct PhysicalEvent {
  std::uint32_t sequence = 0;
  std::uint32_t epoch = 0;
  std::uint32_t at_ms = 0;
  InputKind kind = InputKind::KeyEdge;
  std::uint8_t key = 0;
  std::int16_t value = 0;
};

// SPSC ring. Each side owns one counter; both counters are aligned 32-bit
// atomics and use load/store only. N need not be a power of two.
template <typename T, std::size_t N>
class SpscQueue {
  static_assert(N > 0, "SPSC queue must have capacity");
  // Cortex-M0+ reports is_always_lock_free=false because read-modify-write
  // operations need runtime support. This bridge uses only aligned loads/stores;
  // the pinned target compiler probe verifies inline ldr/str plus dmb barriers.
  static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
                "bridge requires a compact 32-bit atomic representation");
  static_assert(!std::is_pointer<T>::value,
                "bridge entries are copied values, never pointers");
  static_assert(std::is_trivially_copyable<T>::value,
                "bridge payloads must be bounded value types");
 public:
  SpscQueue() noexcept = default;
  explicit SpscQueue(std::uint32_t initial_counter) noexcept
      : head_(initial_counter), tail_(initial_counter),
        head_slot_(initial_counter % N), tail_slot_(initial_counter % N) {}

  bool try_push(const T& value) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (static_cast<std::uint32_t>(head - tail) >= N) return false;
    // GCC 16.1.0 (arm-none-eabi, -O2) reports -Wstringop-overflow here for
    // inlined pushes into SharedLink members (EngineApp::service_diagnostics,
    // UiWorker). It attributes this variable-index store to an unrelated
    // 4-byte std::atomic _M_i. The index is always below N (advance() wraps at N)
    // and the full check above keeps the consumer's slot unwritten, so the store
    // is in bounds. Suppressed only for this statement.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    items_[head_slot_] = value;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    head_slot_ = advance(head_slot_);
    head_.store(head + 1U, std::memory_order_release);
    return true;
  }

  bool try_pop(T& value) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    value = items_[tail_slot_];
    tail_slot_ = advance(tail_slot_);
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
  }

  std::size_t drain_newest(T& newest) noexcept {
    std::size_t count = 0;
    T item{};
    while (count < N && try_pop(item)) {
      newest = item;
      ++count;
    }
    return count;
  }

  bool empty() const noexcept {
    return tail_.load(std::memory_order_relaxed) ==
           head_.load(std::memory_order_acquire);
  }

  // Diagnostic depth (entries currently queued). Exact when called by either
  // endpoint for its own counter; the other counter is an acquire snapshot.
  std::size_t size() const noexcept {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    const auto depth = static_cast<std::uint32_t>(head - tail);
    return depth > N ? N : depth;
  }

 private:
  static constexpr std::size_t advance(std::size_t slot) noexcept {
    return slot + 1U == N ? 0U : slot + 1U;
  }

  alignas(4) std::atomic<std::uint32_t> head_{0};
  alignas(4) std::atomic<std::uint32_t> tail_{0};
  std::size_t head_slot_ = 0;  // Producer owned.
  std::size_t tail_slot_ = 0;  // Consumer owned.
  T items_[N]{};
};

struct PhysicalState {
  std::uint16_t key_mask = 0;
  std::uint32_t fault_generation = 0;
};

// Core 1 is the sole event producer. Core 0 owns epoch changes and fault
// acknowledgement. A fault generation is acknowledged by value, so an
// overflow racing recovery cannot be accidentally cleared.
class PhysicalEventBridge {
 public:
  static constexpr std::size_t kCapacity = 64;
  static constexpr std::uint16_t kAllKeysMask = 0x0fffU;

  bool publish(PhysicalEvent event, std::uint16_t current_key_mask) noexcept {
    current_key_mask_ = static_cast<std::uint16_t>(current_key_mask & kAllKeysMask);
    const auto requested_epoch = epoch_.load(std::memory_order_acquire);
    if (requested_epoch != producer_epoch_) producer_epoch_ = requested_epoch;
    event.sequence = next_sequence_++;
    event.epoch = producer_epoch_;
    if (event.kind == InputKind::Resync)
      event.value = static_cast<std::int16_t>(current_key_mask_);
    if (!events_.try_push(event)) {
      ++fault_generation_;
      publish_state();
      return false;
    }
    publish_state();
    return true;
  }

  bool try_pop(PhysicalEvent& event) noexcept { return events_.try_pop(event); }
  std::size_t queue_depth() const noexcept { return events_.size(); }
  // Producer-side view of the epoch core 0 last requested (acquire).
  std::uint32_t requested_epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }

  std::size_t change_epoch(std::uint32_t epoch) noexcept {
    epoch_.store(epoch, std::memory_order_release);
    awaiting_resync_ = true;
    have_consumer_sequence_ = false;
    std::size_t discarded = 0;
    PhysicalEvent stale{};
    while (discarded < kCapacity && events_.try_pop(stale)) ++discarded;
    return discarded;
  }

  PhysicalState state() const noexcept {
    return {static_cast<std::uint16_t>(physical_mask_.load(std::memory_order_acquire)),
            fault_generation_published_.load(std::memory_order_acquire)};
  }

  bool faulted() const noexcept {
    const auto published = state().fault_generation;
    return published != acknowledged_fault_.load(std::memory_order_acquire);
  }

  void acknowledge_fault(std::uint32_t observed_generation) noexcept {
    if (observed_generation != acknowledged_fault_.load(std::memory_order_relaxed))
      awaiting_resync_ = true;
    acknowledged_fault_.store(observed_generation, std::memory_order_release);
  }

  // After changing epoch, core 0 ignores old events until it observes a
  // current-epoch, all-keys-up resync record.
  bool accept_for_musical_input(const PhysicalEvent& event) noexcept {
    const auto current_epoch = epoch_.load(std::memory_order_acquire);
    if (event.epoch != current_epoch) return false;
    if (have_consumer_sequence_ && event.sequence != expected_sequence_)
      awaiting_resync_ = true;
    expected_sequence_ = event.sequence + 1U;
    have_consumer_sequence_ = true;
    if (faulted()) awaiting_resync_ = true;
    if (awaiting_resync_) {
      if (event.kind != InputKind::Resync || (static_cast<std::uint16_t>(event.value) & kAllKeysMask) != 0)
        return false;
      awaiting_resync_ = false;
      return true;
    }
    return true;
  }

  void require_resync() noexcept { awaiting_resync_ = true; }
  bool awaiting_resync() const noexcept { return awaiting_resync_; }
  bool recovery_required() const noexcept { return faulted() || awaiting_resync_; }

 private:
  void publish_state() noexcept {
    physical_mask_.store(current_key_mask_, std::memory_order_release);
    fault_generation_published_.store(fault_generation_, std::memory_order_release);
  }

  SpscQueue<PhysicalEvent, kCapacity> events_{};
  alignas(4) std::atomic<std::uint32_t> epoch_{0};
  alignas(4) std::atomic<std::uint32_t> physical_mask_{0};
  alignas(4) std::atomic<std::uint32_t> fault_generation_published_{0};
  alignas(4) std::atomic<std::uint32_t> acknowledged_fault_{0};
  std::uint32_t producer_epoch_ = 0;
  std::uint32_t next_sequence_ = 0;
  std::uint32_t fault_generation_ = 0;
  std::uint16_t current_key_mask_ = 0;
  bool awaiting_resync_ = false;  // Core 0 owned.
  std::uint32_t expected_sequence_ = 0;  // Core 0 owned.
  bool have_consumer_sequence_ = false;   // Core 0 owned.
};

template <typename Model>
struct UiSnapshot {
  std::uint32_t revision = 0;
  std::uint32_t epoch = 0;
  Model model{};
};

template <typename Model>
using UiSnapshotQueue = SpscQueue<UiSnapshot<Model>, 3>;

class StartupFlags {
 public:
  void core0_ready() noexcept { core0_.store(1U, std::memory_order_release); }
  void core1_ready() noexcept { core1_.store(1U, std::memory_order_release); }
  bool core0_is_ready() const noexcept { return core0_.load(std::memory_order_acquire) != 0; }
  bool core1_is_ready() const noexcept { return core1_.load(std::memory_order_acquire) != 0; }
  bool all_ready() const noexcept { return core0_is_ready() && core1_is_ready(); }
 private:
  alignas(4) std::atomic<std::uint32_t> core0_{0};
  alignas(4) std::atomic<std::uint32_t> core1_{0};
};

enum class WorkerAction : std::uint8_t { None, Quiesce, Resume };

class MaintenanceHandshake {
 public:
  std::uint32_t request(std::uint32_t now_ms, std::uint32_t timeout_ms) noexcept {
    request_started_ms_ = now_ms;
    timeout_ms_ = timeout_ms <= 0x7fffffffU ? timeout_ms : 0x7fffffffU;
    ++request_sequence_;
    if (request_sequence_ == 0) ++request_sequence_;
    request_.store(request_sequence_, std::memory_order_release);
    pending_ = true;
    return request_sequence_;
  }

  // Call after the worker has finished its current bounded transfer.
  WorkerAction worker_poll() noexcept {
    const auto requested = request_.load(std::memory_order_acquire);
    if (requested != 0 && requested != worker_seen_ &&
        resume_.load(std::memory_order_acquire) != requested) {
      worker_seen_ = requested;
      return WorkerAction::Quiesce;
    }
    if (requested != 0 && resume_.load(std::memory_order_acquire) == requested &&
        worker_quiescent_) {
      worker_quiescent_ = false;
      return WorkerAction::Resume;
    }
    return WorkerAction::None;
  }

  void worker_acknowledge_quiescence() noexcept {
    worker_quiescent_ = true;
    acknowledged_.store(worker_seen_, std::memory_order_release);
  }
  bool acknowledged(std::uint32_t sequence) const noexcept {
    return acknowledged_.load(std::memory_order_acquire) == sequence;
  }
  bool timed_out(std::uint32_t now_ms) const noexcept {
    return pending_ && deadline_reached(now_ms, request_started_ms_ + timeout_ms_);
  }
  void cancel() noexcept {
    resume_.store(request_sequence_, std::memory_order_release);
    pending_ = false;
  }
  // The caller increments PhysicalEventBridge's epoch before this resume and
  // requires a fresh all-keys-up Resync before accepting musical input.
  void resume(std::uint32_t sequence) noexcept {
    resume_.store(sequence, std::memory_order_release);
    pending_ = false;
  }

 private:
  alignas(4) std::atomic<std::uint32_t> request_{0};
  alignas(4) std::atomic<std::uint32_t> acknowledged_{0};
  alignas(4) std::atomic<std::uint32_t> resume_{0};
  std::uint32_t request_sequence_ = 0;       // Core 0 owned.
  std::uint32_t request_started_ms_ = 0;     // Core 0 owned.
  std::uint32_t timeout_ms_ = 0;             // Core 0 owned.
  std::uint32_t worker_seen_ = 0;            // Core 1 owned.
  bool pending_ = false;                     // Core 0 owned.
  bool worker_quiescent_ = false;             // Core 1 owned.
};

}  // namespace orbit
