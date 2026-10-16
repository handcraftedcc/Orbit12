#pragma once

// Headless musical slice composition (not the final T14 application).
//
// Core 0 (EngineApp): USB MIDI poll -> Transport -> Controller::poll -> Output
// flush, plus the read-only CDC diagnostics dump. Core 1 (UiWorker): GPIO and
// encoder scan -> bridge events, NeoPixel feedback and a diagnostics page on
// the SH1106 with 60 s sleep/wake. No Arduino headers here: peripherals are
// reached through platform/macropad adapters. No heap, no Arduino String, no
// waits; every loop pass does bounded work.
//
// Storage is intentionally absent: the controller receives unset storage ports,
// so every scene operation is rejected and counted (no fabricated success) and
// the device runs the default in-RAM scene.

#include <cstddef>
#include <cstdint>

#include "bridge.hpp"
#include "core1_attribution.hpp"
#include "renderer.hpp"
#include "controller.hpp"
#include "ui_model.hpp"
#include "../domain/chain.hpp"
#include "../domain/output.hpp"
#include "../persistence/fs_format.hpp"
#include "../persistence/maintenance_command.hpp"
#include "../persistence/maintenance_protocol.hpp"
#include "../platform/macropad/diag_io.hpp"
#include "../platform/macropad/gpio.hpp"
#include "../platform/macropad/usb_midi.hpp"
#include "maintenance_info.hpp"

namespace orbit {

// Normal-run reply outbox: one INFO frame (header, <= kInfoTextMaxBytes text, CRC) and two NAKs
// that echo a full-length name (582 bytes; see the static_assert in app_main.cpp).
constexpr std::size_t kOutboxBytes = 592;

// Storage supplied by the board glue (LittleFS on the MacroPad; an in-memory fake in
// host tests). mount() never formats. set_window opens/closes the write window.
// format() is the operator-requested erase (maintenance FORMAT frame only).
struct AppStorage {
  void* context = nullptr;
  bool (*mount)(void* context) = nullptr;
  void (*set_window)(void* context, bool open) = nullptr;
  fsformat::FormatResult (*format)(void* context) = nullptr;
  // Reports the adapter's own mounted state. Read after every format outcome for the dump.
  bool (*mounted)(void* context) = nullptr;
  SceneStoragePort port{};
};

// ---- Bounded timing statistics (microseconds) ----
struct LatencyStats {
  static constexpr std::uint8_t kBuckets = 13;      // 16 us << 0..11, then overflow
  static constexpr std::uint32_t kWindowMs = 10000;
  std::uint32_t count = 0;
  std::uint32_t max_us = 0;
  std::uint32_t win_max_us = 0;                     // current 10 s window
  std::uint32_t last_win_max_us = 0;                // previous completed window
  std::uint32_t win_start_ms = 0;
  std::uint64_t sum_us = 0;
  std::uint32_t hist[kBuckets]{};

  void record(std::uint32_t us, std::uint32_t now_ms) noexcept;
  std::uint32_t avg_us() const noexcept { return count == 0 ? 0U : static_cast<std::uint32_t>(sum_us / count); }
  // Upper bound of the histogram bucket holding the 99th percentile, clamped to
  // the observed maximum. Returns max_us while fewer than 100 samples exist.
  std::uint32_t p99_us() const noexcept;
  static std::uint8_t bucket_for(std::uint32_t us) noexcept;
};

// ---- Core 1 -> core 0 statistics (copied through a queue) ----
struct Core1Stats {
  LatencyStats period, scan, render, pixels, compose;
  Core1Attribution attribution;                   // worst iterations by phase, gap and steady state
  std::uint32_t loops = 0, events_published = 0, events_dropped = 0, resyncs = 0;
  std::uint32_t hi_events_per_pass = 0, hi_bridge_depth = 0, hi_ui_depth = 0;
  std::uint32_t snapshots_drained = 0, encoder_glitches = 0;
  std::uint16_t key_mask = 0;
  bool sleeping = false, have_model = false;
  platform::StackReport stack{};
};

// ---- Core 0 -> core 1 summary (counters are plain copies, not shared state) ----
struct DiagSnapshot {
  std::uint32_t uptime_ms = 0, loops = 0;
  std::uint16_t bpm = 0, sounding = 0, owed = 0, pending = 0, keys_held = 0;
  bool running = false, usb_connected = false, external_clock = false, recovering = false;
  std::uint8_t section = 0, active_chain = 0, scene_status = 0;
  std::int8_t active_parm = 0;
  OutputDiagnostics out{};
  ChainDiagnostics chain{};
  ControllerDiagnostics ctl{};
  platform::UsbMidiDiagnostics usb{};
  std::uint32_t bridge_fault_generation = 0;
  LatencyStats period, tick_late, send_late;        // core 0 loop period, clock error
  std::uint32_t work_max_us = 0, multi_tick = 0, ticks = 0;
  std::uint32_t hi_bridge_depth = 0, hi_pending = 0, hi_rx_queue = 0, ui_publish_full = 0;
  std::uint32_t dumps = 0, dump_busy = 0, dump_truncated = 0, dump_build_max_us = 0;
  platform::StackReport stack{};
  platform::HeapReport heap{};
};

// ---- Tiny bounded text builders (no printf, no heap) ----
class TextOut {
 public:
  TextOut(char* buffer, std::size_t capacity) noexcept : buf_(buffer), cap_(capacity) { if (cap_) buf_[0] = '\0'; }
  TextOut& s(const char* text) noexcept;
  TextOut& c(char ch) noexcept;
  TextOut& u(std::uint32_t value) noexcept;
  TextOut& i(std::int32_t value) noexcept;
  TextOut& nl() noexcept { return c('\n'); }
  std::size_t size() const noexcept { return len_; }
  bool overflowed() const noexcept { return overflow_; }
 private:
  char* buf_;
  std::size_t cap_, len_ = 0;
  bool overflow_ = false;
};

// Serial dumps keep this many bytes free at the end of the buffer for the closing lines.
constexpr std::size_t kDumpTailBytes = 64;
// Closes a dump built through body (created over buf with capacity cap - kDumpTailBytes). A body
// that overflowed is followed by "TRUNCATED dumps=N" so a cut is never silent; "END" always ends
// the dump. Returns the total number of bytes in buf.
std::size_t close_dump(const TextOut& body, char* buf, std::size_t cap, std::uint32_t truncated_dumps) noexcept;

// Fixed-width screen line: content is truncated or space padded to 21 columns.
class ScreenLine {
 public:
  explicit ScreenLine(char* line) noexcept : line_(line) {}
  ScreenLine& s(const char* text) noexcept;
  ScreenLine& c(char ch) noexcept;
  ScreenLine& u(std::uint32_t value, std::uint8_t width = 0) noexcept;
  ScreenLine& i(std::int32_t value) noexcept;
  void finish() noexcept;
 private:
  char* line_;
  std::size_t len_ = 0;
};

// Pure composition of the diagnostics page and the serial dump.
void compose_screen(const UiModel* model, const DiagSnapshot* diag, const Core1Stats& core1,
                    platform::TextScreen& out) noexcept;

// ---- Shared state: the only objects both cores touch ----
struct SharedLink {
  PhysicalEventBridge bridge;                     // core 1 -> core 0
  UiSnapshotQueue<UiModel> ui;                    // core 0 -> core 1
  SpscQueue<DiagSnapshot, 2> diag;                // core 0 -> core 1
  SpscQueue<Core1Stats, 2> core1;                 // core 1 -> core 0
  StartupFlags startup;
  MaintenanceHandshake maintenance;               // core 0 requests, core 1 quiesces
};

// Maintenance (T14): entered by holding any pad at power-up (boot.py: "any key held"
// selects USB mode in the source). Core 1 quiesces; core 0 stops and serves the CDC
// frames of maintenance::MaintenanceSession. Exit: a plain 'Q' byte between frames, or 60 s with no
// bytes received.
enum class MaintenanceState : std::uint8_t { Off, Quiescing, Active };

class EngineApp {
 public:
  EngineApp(SharedLink& link, ClockPort clock, platform::UsbMidi& usb, const AppStorage& storage) noexcept;
  void setup() noexcept;                          // core 0, once
  void poll() noexcept;                           // core 0, every loop()
  const Controller& controller() const noexcept { return ctl_; }
  Controller& controller() noexcept { return ctl_; }  // host tests drive scene requests directly
  MaintenanceState maintenance_state() const noexcept { return maint_; }
  const maintenance::MaintenanceSession& maintenance_session() const noexcept { return session_; }
  bool storage_mounted() const noexcept { return storage_mounted_; }
  // True after the boot sequence (KEYBRI, active scene) has finished; false before it starts.
  bool boot_done() const noexcept { return boot_started_ && ctl_.boot_complete(); }

 private:
  ControllerPorts make_ports() const noexcept;
  void note_tick(const TimeSample& now, const TimeSample& after, std::uint32_t tick_before,
                 std::uint32_t scheduled_us, bool eligible) noexcept;
  void fill_diag(std::uint32_t now_ms) noexcept;
  void service_diagnostics(const TimeSample& now) noexcept;
  void service_serial(const TimeSample& now) noexcept;   // normal run: commands, replies, '?' dump
  void build_dump() noexcept;
  void service_gesture(const TimeSample& now) noexcept;
  void service_quiesce(const TimeSample& now) noexcept;
  void service_maintenance_serial(const TimeSample& now) noexcept;
  // Enters maintenance (stop, quiesce, window). from_host: the ACK or NAK of ENTER-MAINT follows.
  bool start_maintenance(const TimeSample& now, bool from_host) noexcept;
  void end_maintenance() noexcept;
  // Normal-run commands (framer sink) and their replies.
  static void run_command(void* context, const maintenance::RunCommand& command) noexcept;
  static void run_passthrough(void* context, std::uint8_t byte) noexcept;
  void handle_run_command(const maintenance::RunCommand& command) noexcept;
  maintenance::NakReason host_entry_refusal() noexcept;   // None: ENTER may start now
  std::size_t compose_info() noexcept;                    // INFO text into info_text_
  bool queue_frame(maintenance::FrameType type, const std::uint8_t* name, std::size_t name_length,
                   const std::uint8_t* payload, std::size_t payload_length) noexcept;
  void queue_nak(const std::uint8_t* name, std::size_t name_length, maintenance::NakReason reason) noexcept;
  void drain_outbox() noexcept;
  void send_frame_blocking(maintenance::FrameType type, const std::uint8_t* name, std::size_t name_length,
                           const std::uint8_t* payload, std::size_t payload_length) noexcept;
  static bool maintenance_write(void* context, const std::uint8_t* data, std::size_t length) noexcept;
  static bool maintenance_stopped(void* context) noexcept;
  static fsformat::FormatResult maintenance_format(void* context) noexcept;

  SharedLink& link_;
  ClockPort clock_;
  platform::UsbMidi& usb_;
  AppStorage storage_;
  Controller ctl_;
  maintenance::MaintenanceSession session_;
  MidiPort port_{};
  DiagSnapshot diag_{};
  Core1Stats core1_{};
  bool have_core1_ = false;
  std::uint32_t last_start_us_ = 0;
  bool have_last_ = false;
  std::uint32_t last_diag_ms_ = 0;
  platform::StackReport stack_cache_{};
  platform::HeapReport heap_cache_{};
  bool boot_started_ = false;
  bool storage_mounted_ = false;
  std::uint32_t mount_heap_bytes_ = 0;
  // Normal-run command framer (every received byte) and its bounded outbox. Replies are queued
  // and drained without waiting; a reply that does not fit is dropped and counted.
  maintenance::RunCommandFramer run_framer_{};
  std::uint8_t outbox_[kOutboxBytes]{};
  std::size_t out_len_ = 0, out_sent_ = 0;
  char info_text_[kInfoTextMaxBytes]{};
  std::uint32_t run_now_ms_ = 0;
  bool enter_pending_ = false;                  // an ENTER-MAINT is waiting for Active or a timeout
  std::uint32_t enter_accepted_ = 0, enter_refused_ = 0, info_served_ = 0, reply_dropped_ = 0;
  // Maintenance entry and exit (core 0 only).
  MaintenanceState maint_ = MaintenanceState::Off;
  bool entry_sampled_ = false;   // the first core-1 pad sample has decided the gesture
  bool entry_armed_ = false;     // only pads held at power-up can enter (boot.py semantics)
  bool write_stalled_ = false;   // a host stopped reading: writes fail at once until a byte arrives
  bool combo_held_ = false;
  std::uint32_t combo_since_ms_ = 0;
  std::uint32_t maint_seq_ = 0, maint_started_ms_ = 0, last_maint_byte_ms_ = 0;
  std::uint32_t maint_entries_ = 0, maint_exits_ = 0, maint_quiesce_timeouts_ = 0, maint_idle_exits_ = 0;
  static constexpr std::uint32_t kEntryWindowMs = 5000;     // gesture accepted in the first 5 s
  static constexpr std::uint32_t kEntryHoldMs = 500;        // any pad held this long
  static constexpr std::uint32_t kQuiesceTimeoutMs = 1000;
  static constexpr std::uint32_t kMaintenanceIdleMs = 60000;
  static constexpr std::uint32_t kWriteStallMs = 2000;      // bounded wait for a host to read
  static constexpr std::size_t kSerialBytesPerPass = 64;
  static constexpr std::size_t kRunBytesPerPass = 32;     // normal run: bytes read per pass
  static constexpr std::size_t kDumpCapacity = 4096;  // single instance (g_engine); +1024 B static RAM
  char dump_[kDumpCapacity]{};
  std::size_t dump_len_ = 0, dump_sent_ = 0;
  bool dump_requested_ = false;
};

class UiWorker {
 public:
  UiWorker(SharedLink& link, ClockPort clock, platform::GpioInput& gpio) noexcept;
  void setup() noexcept;                          // core 1, once
  void poll() noexcept;                           // core 1, every loop1()

 private:
  bool emit(InputKind kind, std::uint8_t key, std::int16_t value, std::uint32_t at_ms,
            std::uint16_t mask) noexcept;
  void apply_pixels(std::uint32_t now_ms) noexcept;
  void render(std::uint32_t now_ms) noexcept;

  SharedLink& link_;
  ClockPort clock_;
  platform::GpioInput& gpio_;
  Core1Stats stats_{};
  UiSnapshot<UiModel> snapshot_{};
  DiagSnapshot diag_{};
  // T12 renderer state: const sheets, one 1 KB frame, and the screen phase.
  // All static members of this worker; nothing is allocated or kept on the stack.
  render::Assets assets_{};
  render::Frame frame_{};
  render::ScreenPower power_{};
  render::ScreenPhase last_phase_ = render::ScreenPhase::Splash;
  RgbColor shown_pixels_[kKeys]{};
  bool have_model_ = false, have_diag_ = false, pixels_dirty_ = true;
  bool assets_ready_ = false, need_render_ = false;
  bool resync_pending_ = false;
  bool quiescent_ = false;                    // acknowledged a maintenance/scene quiesce
  std::uint32_t seen_epoch_ = 0, seen_fault_ = 0;
  std::uint32_t last_start_us_ = 0, last_render_ms_ = 0;
  std::uint32_t last_pixel_ms_ = 0, last_stats_ms_ = 0, last_stack_ms_ = 0;
  bool have_last_ = false;
  std::uint16_t last_mask_ = 0;
  Core1Iteration iteration_{};                // phases of the body in progress
  std::uint32_t last_end_us_ = 0;             // end of the previous body (gap origin)
};

constexpr std::uint32_t kScreenSleepMs = 60000;   // Orion.py SCREEN_SLEEP_TIME

}  // namespace orbit
