#include "app/app_main.hpp"

namespace orbit {

// ---------------------------------------------------------------------------
// LatencyStats
// ---------------------------------------------------------------------------
std::uint8_t LatencyStats::bucket_for(std::uint32_t us) noexcept {
  for (std::uint8_t i = 0; i < kBuckets - 1U; ++i) {
    if (us <= (16U << i)) return i;
  }
  return kBuckets - 1U;
}

void LatencyStats::record(std::uint32_t us, std::uint32_t now_ms) noexcept {
  if (count == 0) win_start_ms = now_ms;
  ++count;
  sum_us += us;
  if (us > max_us) max_us = us;
  if (elapsed(now_ms, win_start_ms) >= kWindowMs) {
    last_win_max_us = win_max_us;
    win_max_us = 0;
    win_start_ms = now_ms;
  }
  if (us > win_max_us) win_max_us = us;
  ++hist[bucket_for(us)];
}

std::uint32_t LatencyStats::p99_us() const noexcept {
  if (count < 100U) return max_us;
  const std::uint32_t rank = count - count / 100U;
  std::uint32_t cumulative = 0;
  for (std::uint8_t i = 0; i < kBuckets; ++i) {
    cumulative += hist[i];
    if (cumulative >= rank) {
      const std::uint32_t bound = i < kBuckets - 1U ? (16U << i) : max_us;
      return bound < max_us ? bound : max_us;
    }
  }
  return max_us;
}

// ---------------------------------------------------------------------------
// Text builders
// ---------------------------------------------------------------------------
TextOut& TextOut::c(char ch) noexcept {
  if (len_ + 1U < cap_) {
    buf_[len_++] = ch;
    buf_[len_] = '\0';
  } else {
    overflow_ = true;
  }
  return *this;
}
TextOut& TextOut::s(const char* text) noexcept {
  if (text == nullptr) text = "?";
  while (*text != '\0') c(*text++);
  return *this;
}
TextOut& TextOut::u(std::uint32_t value) noexcept {
  char digits[10];
  std::uint8_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + value % 10U);
    value /= 10U;
  } while (value != 0 && n < 10U);
  while (n != 0) c(digits[--n]);
  return *this;
}
TextOut& TextOut::i(std::int32_t value) noexcept {
  if (value < 0) {
    c('-');
    return u(0U - static_cast<std::uint32_t>(value));
  }
  return u(static_cast<std::uint32_t>(value));
}

std::size_t close_dump(const TextOut& body, char* buf, std::size_t cap, std::uint32_t truncated_dumps) noexcept {
  TextOut tail(buf + body.size(), cap - body.size());
  if (body.overflowed()) tail.nl().s("TRUNCATED dumps=").u(truncated_dumps).nl();
  tail.s("END").nl();
  return body.size() + tail.size();
}

ScreenLine& ScreenLine::c(char ch) noexcept {
  if (len_ < platform::kScreenColumns) line_[len_++] = ch;
  return *this;
}
ScreenLine& ScreenLine::s(const char* text) noexcept {
  if (text == nullptr) text = "?";
  while (*text != '\0') c(*text++);
  return *this;
}
ScreenLine& ScreenLine::u(std::uint32_t value, std::uint8_t width) noexcept {
  char digits[10];
  std::uint8_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + value % 10U);
    value /= 10U;
  } while (value != 0 && n < 10U);
  for (std::uint8_t pad = n; pad < width; ++pad) c('0');
  while (n != 0) c(digits[--n]);
  return *this;
}
ScreenLine& ScreenLine::i(std::int32_t value) noexcept {
  if (value < 0) {
    c('-');
    return u(0U - static_cast<std::uint32_t>(value));
  }
  return u(static_cast<std::uint32_t>(value));
}
void ScreenLine::finish() noexcept {
  while (len_ < platform::kScreenColumns) line_[len_++] = ' ';
  line_[platform::kScreenColumns] = '\0';
}

void compose_screen(const UiModel* model, const DiagSnapshot* diag, const Core1Stats& c1,
                    platform::TextScreen& out) noexcept {
  ScreenLine l0(out.line[0]), l1(out.line[1]), l2(out.line[2]), l3(out.line[3]);
  ScreenLine l4(out.line[4]), l5(out.line[5]), l6(out.line[6]), l7(out.line[7]);
  if (model != nullptr) {
    const std::uint8_t chain = model->active_chain < kSlotCount ? model->active_chain : 0;
    l0.c('S').u(static_cast<std::uint32_t>(model->section)).c(' ').s(model->slots[chain].label)
        .s(" P").i(model->active_parm).s(" C").u(chain);
  } else {
    l0.s("WAIT UI SNAPSHOT");
  }
  if (diag != nullptr) {
    l1.s("BPM").u(diag->bpm, 3).c(' ').s(diag->running ? "RUN" : "STP").c(' ')
        .s(diag->external_clock ? "EXT" : "INT").s(" K").u(diag->keys_held).s(" H").u(diag->sounding);
    l2.c('Q').u(diag->pending).s(" O").u(diag->owed).s(" RF").u(diag->out.refused)
        .s(" OV").u(diag->out.queue_overflow).s(" DF").u(diag->out.owed_deferred)
        .s(" U").u(diag->usb_connected ? 1U : 0U);
    l3.s("CH b").u(diag->chain.batch_overflow).s(" s").u(diag->chain.scratch_overflow)
        .s(" o").u(diag->chain.output_overflow).s(" R").u(diag->ctl.chain_recoveries)
        .s(" F").u(diag->bridge_fault_generation).s(" G").u(diag->ctl.events_gated);
    l4.s("C0 a").u(diag->period.avg_us()).s(" p").u(diag->period.p99_us())
        .s(" m").u(diag->period.max_us).s(" w").u(diag->period.last_win_max_us);
    l5.s("CK a").u(diag->tick_late.avg_us()).s(" p").u(diag->tick_late.p99_us())
        .s(" m").u(diag->tick_late.max_us).s(" S").u(diag->send_late.max_us);
    l6.s("HB").u(diag->hi_bridge_depth).s(" HQ").u(diag->hi_pending).s(" HU").u(c1.hi_ui_depth)
        .s(" RX").u(diag->hi_rx_queue).s(" T").u(diag->usb.tx_refused);
  } else {
    l1.s("WAIT CORE0 DIAG");
  }
  l7.s("C1 a").u(c1.period.avg_us()).s(" p").u(c1.period.p99_us()).s(" m").u(c1.period.max_us)
      .s(" R").u(c1.render.max_us);
  l0.finish();
  l1.finish();
  l2.finish();
  l3.finish();
  l4.finish();
  l5.finish();
  l6.finish();
  l7.finish();
}

// ---------------------------------------------------------------------------
// EngineApp (core 0)
// ---------------------------------------------------------------------------
ControllerPorts EngineApp::make_ports() const noexcept {
  // Scene storage and the core-1 quiesce handshake. An unmounted partition still gives a
  // valid port: every operation then reports an error, and boot falls back to defaults.
  ControllerPorts ports{};
  ports.storage = storage_.port;
  ports.maintenance = &link_.maintenance;
  ports.window.context = storage_.context;
  ports.window.set_open = storage_.set_window;
  return ports;
}

EngineApp::EngineApp(SharedLink& link, ClockPort clock, platform::UsbMidi& usb, const AppStorage& storage) noexcept
    : link_(link), clock_(clock), usb_(usb), storage_(storage), ctl_(make_ports()),
      session_(storage.port, ctl_.scene_schema(), ctl_.scene_service(),
               maintenance::MaintenanceOutput{this, &EngineApp::maintenance_write},
               maintenance::MaintenanceGate{this, &EngineApp::maintenance_stopped}) {
  session_.set_format(maintenance::MaintenanceFormatPort{this, &EngineApp::maintenance_format});
}

void EngineApp::setup() noexcept {
  platform::stack_paint(0);
  (void)usb_.begin(true);  // MIDI, then CDC serial, then re-enumerate (T00 order)
  port_ = usb_.port();
  const TimeSample now = clock_.now();
  // Mount without formatting. The heap delta is library initialization cost (measured).
  const std::uint32_t heap_before = platform::heap_report().used;
  storage_mounted_ = storage_.mount != nullptr && storage_.mount(storage_.context);
  const std::uint32_t heap_after = platform::heap_report().used;
  mount_heap_bytes_ = heap_after >= heap_before ? heap_after - heap_before : 0U;
  ctl_.init(now);
  link_.startup.core0_ready();
}

// ---- Maintenance (core 0) ----------------------------------------------------
// Entry: any pad held for kEntryHoldMs during the first kEntryWindowMs of uptime. The
// pad state comes from core 1's statistics (boot-held keys produce no edges).
void EngineApp::service_gesture(const TimeSample& now) noexcept {
  if (maint_ != MaintenanceState::Off || !boot_started_ || !have_core1_) return;
  // The first pad sample decides for this boot. Keys held at power-up produce no edges, so
  // only the mask says they were down; a pad pressed later cannot enter maintenance.
  if (!entry_sampled_) {
    entry_sampled_ = true;
    entry_armed_ = core1_.key_mask != 0;
  }
  if (!entry_armed_ || now.ms >= kEntryWindowMs || core1_.key_mask == 0) { combo_held_ = false; return; }
  if (!combo_held_) {
    combo_held_ = true;
    combo_since_ms_ = now.ms;
    return;
  }
  if (elapsed(now.ms, combo_since_ms_) < kEntryHoldMs) return;
  if (!ctl_.enter_maintenance()) return;  // a scene operation or boot load is still running
  maint_ = MaintenanceState::Quiescing;
  ++maint_entries_;
  maint_seq_ = link_.maintenance.request(now.ms, kQuiesceTimeoutMs);
  maint_started_ms_ = now.ms;
  last_maint_byte_ms_ = now.ms;
  write_stalled_ = false;
}

// Stopped = transport stopped and pending releases accepted (the controller drains them in
// poll). Core 1 must acknowledge the quiesce before any storage write window opens.
void EngineApp::service_quiesce(const TimeSample& now) noexcept {
  if (link_.maintenance.acknowledged(maint_seq_) && ctl_.is_stopped()) {
    if (storage_.set_window != nullptr) storage_.set_window(storage_.context, true);
    maint_ = MaintenanceState::Active;
    last_maint_byte_ms_ = now.ms;
    return;
  }
  if (elapsed(now.ms, maint_started_ms_) >= kQuiesceTimeoutMs) {
    // Never block a running core: cancel the request and stay in normal performance.
    link_.maintenance.cancel();
    ctl_.leave_maintenance();
    maint_ = MaintenanceState::Off;
    ++maint_quiesce_timeouts_;
  }
}

void EngineApp::service_maintenance_serial(const TimeSample& now) noexcept {
  for (std::size_t n = 0; n < kSerialBytesPerPass; ++n) {
    const int byte = platform::serial_read();
    if (byte < 0) break;
    last_maint_byte_ms_ = now.ms;
    write_stalled_ = false;  // the host is reading again: writes may wait once more
    if (byte == 'Q' && !session_.mid_frame()) {
      end_maintenance();
      return;
    }
    session_.feed(static_cast<std::uint8_t>(byte));
  }
  if (elapsed(now.ms, last_maint_byte_ms_) >= kMaintenanceIdleMs) {
    ++maint_idle_exits_;
    end_maintenance();
  }
}

void EngineApp::end_maintenance() noexcept {
  session_.abort();  // removes an unfinished .tmp; targets are never touched
  if (storage_.set_window != nullptr) storage_.set_window(storage_.context, false);
  link_.maintenance.resume(maint_seq_);
  ctl_.leave_maintenance();  // epoch change: physical events are resynced before notes
  maint_ = MaintenanceState::Off;
  combo_held_ = false;
  ++maint_exits_;
}

// FORMAT runs only inside the session (stopped, window open). The mounted flag reported in
// the dump follows the outcome, including a failed format that left the partition unmounted.
fsformat::FormatResult EngineApp::maintenance_format(void* context) noexcept {
  auto& self = *static_cast<EngineApp*>(context);
  if (self.storage_.format == nullptr) return fsformat::FormatResult::FormatFailed;
  const fsformat::FormatResult result = self.storage_.format(self.storage_.context);
  // Taken from the adapter, not inferred from the result: a refused format leaves it unchanged.
  self.storage_mounted_ = self.storage_.mounted != nullptr && self.storage_.mounted(self.storage_.context);
  return result;
}

bool EngineApp::maintenance_stopped(void* context) noexcept {
  const auto& self = *static_cast<const EngineApp*>(context);
  return self.maint_ == MaintenanceState::Active && self.ctl_.is_stopped();
}

// Writes a whole response. Each wait is bounded: a host that stops reading is abandoned
// after kWriteStallMs (the engine is stopped here, so waiting never sounds notes).
bool EngineApp::maintenance_write(void* context, const std::uint8_t* data, std::size_t length) noexcept {
  auto& self = *static_cast<EngineApp*>(context);
  // The session keeps sending chunks after a failed write (GET streams the whole file), so a
  // stalled host must fail every later chunk at once, or each chunk would wait the full bound.
  if (self.write_stalled_) return false;
  const std::uint32_t start = self.clock_.now().ms;
  std::size_t done = 0;
  while (done < length) {
    std::size_t room = platform::serial_writable();
    if (room == 0) {
      if (elapsed(self.clock_.now().ms, start) >= kWriteStallMs) {
        self.write_stalled_ = true;
        return false;
      }
      self.usb_.poll();  // keep the endpoints serviced while the host drains
      continue;
    }
    if (room > length - done) room = length - done;
    const std::size_t wrote = platform::serial_write(reinterpret_cast<const char*>(data + done), room);
    if (wrote == 0) {  // no host holds the port open
      self.write_stalled_ = true;
      return false;
    }
    done += wrote;
  }
  return true;
}

void EngineApp::note_tick(const TimeSample& now, const TimeSample& after, std::uint32_t tick_before,
                          std::uint32_t scheduled_us, bool eligible) noexcept {
  if (!eligible || !ctl_.transport().running()) return;
  const std::uint32_t delta = ctl_.transport().tick() - tick_before;
  if (delta == 0) return;
  ++diag_.ticks;
  if (delta > 1U) ++diag_.multi_tick;
  const std::int32_t late = static_cast<std::int32_t>(now.us - scheduled_us);
  const std::int32_t sent = static_cast<std::int32_t>(after.us - scheduled_us);
  diag_.tick_late.record(late > 0 ? static_cast<std::uint32_t>(late) : 0U, now.ms);
  diag_.send_late.record(sent > 0 ? static_cast<std::uint32_t>(sent) : 0U, after.ms);
}

void EngineApp::poll() noexcept {
  const TimeSample now = clock_.now();
  if (have_last_) diag_.period.record(now.us - last_start_us_, now.ms);
  last_start_us_ = now.us;
  have_last_ = true;
  ++diag_.loops;

  usb_.poll();
  const std::uint32_t rx_depth = static_cast<std::uint32_t>(usb_.rx_size());
  if (rx_depth > diag_.hi_rx_queue) diag_.hi_rx_queue = rx_depth;
  const std::uint32_t bridge_depth = static_cast<std::uint32_t>(link_.bridge.queue_depth());
  if (bridge_depth > diag_.hi_bridge_depth) diag_.hi_bridge_depth = bridge_depth;

  // Boot: once core 1 is ready, load KEYBRI and the active scene (nonblocking, in poll).
  if (!boot_started_ && link_.startup.all_ready()) {
    boot_started_ = true;
    (void)ctl_.begin_boot();
  }
  service_gesture(now);

  const bool maintenance = maint_ != MaintenanceState::Off;
  const Transport& transport = ctl_.transport();
  const std::uint32_t tick_before = transport.tick();
  const std::uint32_t scheduled_us = transport.next_us();
  const bool eligible = !maintenance && transport.running() &&
                        ctl_.global().transport_mode == TransportMode::Internal;

  PollArgs args;
  args.now = now;
  args.midi_in = usb_.rx_data();
  args.midi_in_count = usb_.rx_size();
  args.port = &port_;
  args.midi_connected = usb_.connected();
  const PollResult result = ctl_.poll(link_.bridge, args);
  // Maintenance discards incoming MIDI so that nothing replays after the exit.
  usb_.rx_consume(maintenance ? usb_.rx_size() : result.midi_consumed);

  const TimeSample after = clock_.now();
  note_tick(now, after, tick_before, scheduled_us, eligible);
  const std::uint32_t work = after.us - now.us;
  if (work > diag_.work_max_us) diag_.work_max_us = work;
  const std::uint32_t pending = ctl_.output().pending();
  if (pending > diag_.hi_pending) diag_.hi_pending = pending;

  if (link_.ui.size() >= 3U) ++diag_.ui_publish_full;
  (void)ctl_.publish_ui(link_.ui, ctl_.epoch(), after.ms);

  if (maint_ == MaintenanceState::Quiescing) service_quiesce(after);
  else if (maint_ == MaintenanceState::Active) service_maintenance_serial(after);
  service_diagnostics(after);
  if (maint_ == MaintenanceState::Off) service_serial();  // '?' dump: normal run only
}

void EngineApp::fill_diag(std::uint32_t now_ms) noexcept {
  diag_.uptime_ms = now_ms;
  diag_.bpm = ctl_.global().bpm;
  diag_.running = ctl_.transport().running();
  diag_.external_clock = ctl_.global().transport_mode == TransportMode::External;
  diag_.usb_connected = usb_.connected();
  diag_.recovering = ctl_.output().recovering();
  std::uint16_t keys = 0;
  for (std::uint16_t mask = ctl_.held_keys(); mask != 0; mask = static_cast<std::uint16_t>(mask >> 1))
    keys = static_cast<std::uint16_t>(keys + (mask & 1U));
  diag_.keys_held = keys;
  diag_.sounding = ctl_.output().held_count();
  diag_.owed = ctl_.output().owed_count();
  diag_.pending = ctl_.output().pending();
  diag_.section = static_cast<std::uint8_t>(ctl_.section());
  diag_.active_chain = ctl_.active_chain();
  diag_.active_parm = ctl_.active_parm();
  diag_.scene_status = static_cast<std::uint8_t>(ctl_.last_scene_status());
  diag_.out = ctl_.output().diagnostics();
  diag_.chain = ctl_.chain().diagnostics();
  diag_.ctl = ctl_.diagnostics();
  diag_.usb = usb_.diagnostics();
  diag_.bridge_fault_generation = link_.bridge.state().fault_generation;
  diag_.stack = stack_cache_;
  diag_.heap = heap_cache_;
}

void EngineApp::service_diagnostics(const TimeSample& now) noexcept {
  if (link_.core1.size() != 0 && link_.core1.drain_newest(core1_) != 0) have_core1_ = true;
  // Stack/heap are sampled only inside build_dump(): heap_report() takes the
  // malloc lock and the stack scan is O(2 KB), neither belongs in the loop.
  if (elapsed(now.ms, last_diag_ms_) >= 100U) {
    last_diag_ms_ = now.ms;
    fill_diag(now.ms);
    (void)link_.diag.try_push(diag_);  // full: the display keeps the previous summary
  }
}

void EngineApp::service_serial() noexcept {
  if (dump_sent_ < dump_len_) {
    const std::size_t room = platform::serial_writable();
    std::size_t n = dump_len_ - dump_sent_;
    if (n > room) n = room;
    if (n != 0) {
      const std::size_t wrote = platform::serial_write(dump_ + dump_sent_, n);
      // 0 means no host holds the port open: abandon the dump, never wait.
      dump_sent_ = wrote == 0 ? dump_len_ : dump_sent_ + wrote;
    }
  }
  for (std::uint8_t n = 0; n < 8U; ++n) {
    const int byte = platform::serial_read();
    if (byte < 0) break;
    if (byte == '?') dump_requested_ = true;
  }
  if (dump_requested_) {
    dump_requested_ = false;
    if (dump_sent_ < dump_len_) ++diag_.dump_busy;
    else build_dump();
  }
}

namespace {
void stats_line(TextOut& o, const char* name, const LatencyStats& st) noexcept {
  o.s(name).s(" n=").u(st.count).s(" avg=").u(st.avg_us()).s(" p99=").u(st.p99_us())
      .s(" max=").u(st.max_us).s(" win=").u(st.last_win_max_us).s("/").u(st.win_max_us).nl();
}
void stack_line(TextOut& o, const char* name, const platform::StackReport& st) noexcept {
  o.s(name).s(" valid=").u(st.valid ? 1U : 0U).s(" cap=").u(st.capacity).s(" nominal=").u(st.nominal)
      .s(" used=").u(st.used)
      .s(" sp=").u(st.sp).s(" bottom=").u(st.bottom).s(" top=").u(st.top).nl();
}
}  // namespace

void EngineApp::build_dump() noexcept {
  // Formatting ~2 KB costs milliseconds on this core. Time it, and drop the next
  // period sample so the dump does not masquerade as loop jitter; a tick due
  // during the dump would still be late, which dump_build_us lets the reader discount.
  const TimeSample now = clock_.now();
  stack_cache_ = platform::stack_report(0);
  heap_cache_ = platform::heap_report();
  ++diag_.dumps;
  fill_diag(now.ms);
  TextOut o(dump_, kDumpCapacity - kDumpTailBytes);  // tail reserved for close_dump()
  o.s("ORBIT12-SLICE up_ms=").u(diag_.uptime_ms).s(" loops=").u(diag_.loops).nl();
  o.s("SIZES controller=").u(sizeof(Controller)).s(" engine=").u(sizeof(EngineApp))
      .s(" uiworker=").u(sizeof(UiWorker)).s(" link=").u(sizeof(SharedLink))
      .s(" ui_model=").u(sizeof(UiModel)).s(" diag=").u(sizeof(DiagSnapshot))
      .s(" core1stats=").u(sizeof(Core1Stats)).s(" bridge=").u(sizeof(PhysicalEventBridge))
      .s(" output=").u(sizeof(Output)).s(" chain=").u(sizeof(Chain))
      .s(" scene_svc=").u(sizeof(SceneService)).nl();
  o.s("HEAP total=").u(diag_.heap.total).s(" used=").u(diag_.heap.used).s(" free=").u(diag_.heap.free).nl();
  stack_line(o, "STACK0", diag_.stack);
  stack_line(o, "STACK1", core1_.stack);
  stats_line(o, "C0_PERIOD_US", diag_.period);
  o.s("C0_WORK_MAX_US=").u(diag_.work_max_us).s(" DUMP_BUILD_MAX_US=").u(diag_.dump_build_max_us).nl();
  o.s("TICKS n=").u(diag_.ticks).s(" multi=").u(diag_.multi_tick).nl();
  stats_line(o, "TICK_LATE_US", diag_.tick_late);
  stats_line(o, "SEND_LATE_US", diag_.send_late);
  o.s("HIWATER bridge=").u(diag_.hi_bridge_depth).s(" out_pending=").u(diag_.hi_pending)
      .s(" usb_rx=").u(diag_.hi_rx_queue).s(" ui_full=").u(diag_.ui_publish_full)
      .s(" c1_ui=").u(core1_.hi_ui_depth).s(" c1_bridge=").u(core1_.hi_bridge_depth)
      .s(" c1_events_pass=").u(core1_.hi_events_per_pass).nl();
  o.s("TRANSPORT bpm=").u(diag_.bpm).s(" running=").u(diag_.running ? 1U : 0U)
      .s(" ext=").u(diag_.external_clock ? 1U : 0U).s(" usb=").u(diag_.usb_connected ? 1U : 0U)
      .s(" keys=").u(diag_.keys_held).s(" sounding=").u(diag_.sounding).s(" owed=").u(diag_.owed)
      .s(" pending=").u(diag_.pending).s(" recovering=").u(diag_.recovering ? 1U : 0U).nl();
  o.s("UI section=").u(diag_.section).s(" chain=").u(diag_.active_chain).s(" parm=").i(diag_.active_parm)
      .s(" scene_status=").u(diag_.scene_status).nl();
  o.s("OUT refused=").u(diag_.out.refused).s(" queue_overflow=").u(diag_.out.queue_overflow)
      .s(" owed_deferred=").u(diag_.out.owed_deferred).s(" recoveries=").u(diag_.out.recoveries)
      .s(" disconnected=").u(diag_.out.disconnected).nl();
  o.s("CHAIN invalid_config=").u(diag_.chain.invalid_config)
      .s(" scratch_overflow=").u(diag_.chain.scratch_overflow)
      .s(" output_overflow=").u(diag_.chain.output_overflow)
      .s(" batch_overflow=").u(diag_.chain.batch_overflow).nl();
  o.s("CTL emergency=").u(diag_.ctl.emergency_releases).s(" gated=").u(diag_.ctl.events_gated)
      .s(" busy=").u(diag_.ctl.events_busy).s(" resync_rel=").u(diag_.ctl.resync_releases)
      .s(" param_rej=").u(diag_.ctl.param_rejected).s(" chain_rec=").u(diag_.ctl.chain_recoveries)
      .s(" scene_fail=").u(diag_.ctl.scene_failures).s(" scene_ign=").u(diag_.ctl.scene_requests_ignored)
      .s(" release_calls=").u(diag_.ctl.release_calls).s(" dev_rel=").u(diag_.ctl.deviation_releases).nl();
  o.s("STORAGE mounted=").u(storage_mounted_ ? 1U : 0U).s(" mount_heap=").u(mount_heap_bytes_)
      .s(" boot=").u(ctl_.boot_complete() ? 1U : 0U).s(" scene=").u(ctl_.active_scene())
      .s(" status=").u(static_cast<std::uint32_t>(ctl_.last_scene_status()))
      .s(" failed=").u(ctl_.scene_failed() ? 1U : 0U)
      .s(" boot_defaults=").u(diag_.ctl.boot_default_fallbacks)
      .s(" settings_saves=").u(diag_.ctl.settings_saves).nl();
  o.s("MAINT state=").u(static_cast<std::uint32_t>(maint_)).s(" entries=").u(maint_entries_)
      .s(" exits=").u(maint_exits_).s(" qtimeout=").u(maint_quiesce_timeouts_)
      .s(" idle_exit=").u(maint_idle_exits_)
      .s(" accepted=").u(session_.counters().accepted).s(" rejected=").u(session_.counters().rejected)
      .s(" aborted=").u(session_.counters().aborted).s(" out_fail=").u(session_.counters().output_failures)
      .nl();
  o.s("BRIDGE fault_gen=").u(diag_.bridge_fault_generation).nl();
  o.s("USB tx_refused=").u(diag_.usb.tx_refused).s(" tx_invalid=").u(diag_.usb.tx_invalid)
      .s(" rx_overflow=").u(diag_.usb.rx_overflow).s(" rx_ignored=").u(diag_.usb.rx_ignored)
      .s(" rx_reserved=").u(diag_.usb.rx_reserved).nl();
  if (have_core1_) {
    o.s("C1 loops=").u(core1_.loops).s(" pub=").u(core1_.events_published)
        .s(" dropped=").u(core1_.events_dropped).s(" resyncs=").u(core1_.resyncs)
        .s(" drained=").u(core1_.snapshots_drained).s(" enc_glitch=").u(core1_.encoder_glitches)
        .s(" sleeping=").u(core1_.sleeping ? 1U : 0U).s(" mask=").u(core1_.key_mask).nl();
    stats_line(o, "C1_PERIOD_US", core1_.period);
    stats_line(o, "C1_SCAN_US", core1_.scan);
    stats_line(o, "C1_RENDER_US", core1_.render);
    stats_line(o, "C1_PIXELS_US", core1_.pixels);
    stats_line(o, "C1_COMPOSE_US", core1_.compose);
    // Worst body and the gap before it, by phase; top= names the largest contributor.
    write_core1_worst(o, "C1_WORST_ALL", core1_.attribution.all);
    write_core1_worst(o, "C1_WORST_STEADY", core1_.attribution.steady);
    o.s("C1_SLOW periods=").u(core1_.attribution.slow_periods).s(" gaps=").u(core1_.attribution.slow_gaps)
        .s(" steady_periods=").u(core1_.attribution.steady_slow_periods).s(" steady_after_ms=")
        .u(kCore1SteadyAfterMs).nl();
  } else {
    o.s("C1 no statistics received yet").nl();
  }
  if (o.overflowed()) ++diag_.dump_truncated;
  dump_len_ = close_dump(o, dump_, kDumpCapacity, diag_.dump_truncated);
  dump_sent_ = 0;
  const std::uint32_t cost = clock_.now().us - now.us;
  if (cost > diag_.dump_build_max_us) diag_.dump_build_max_us = cost;
  have_last_ = false;  // skip the next period sample
}

// ---------------------------------------------------------------------------
// UiWorker (core 1)
// ---------------------------------------------------------------------------
UiWorker::UiWorker(SharedLink& link, ClockPort clock, platform::GpioInput& gpio) noexcept
    : link_(link), clock_(clock), gpio_(gpio) {}

void UiWorker::setup() noexcept {
  platform::stack_paint(1);
  platform::display_begin();
  platform::display_splash();
  platform::pixels_begin();
  gpio_.begin(false);
  const TimeSample now = clock_.now();
  power_.start(now.ms);  // launch image for 1.5 s (code.py), then the UI
  last_phase_ = power_.phase();
  assets_ready_ = assets_.load();
  seen_epoch_ = link_.bridge.requested_epoch();
  seen_fault_ = link_.bridge.state().fault_generation;
  link_.startup.core1_ready();
}

bool UiWorker::emit(InputKind kind, std::uint8_t key, std::int16_t value, std::uint32_t at_ms,
                    std::uint16_t mask) noexcept {
  PhysicalEvent event;
  event.kind = kind;
  event.key = key;
  event.value = value;
  event.at_ms = at_ms;
  if (link_.bridge.publish(event, mask)) {
    ++stats_.events_published;
    return true;
  }
  ++stats_.events_dropped;  // the bridge latched a fault; Resync follows once all keys are up
  return false;
}

void UiWorker::apply_pixels(std::uint32_t now_ms) noexcept {
  if (!have_model_ || !pixels_dirty_ || elapsed(now_ms, last_pixel_ms_) < 10U) return;
  const std::uint32_t t0 = clock_.now().us;
  platform::pixels_show(snapshot_.model.pixels, kKeys);
  stats_.pixels.record(clock_.now().us - t0, now_ms);
  for (std::size_t i = 0; i < kKeys; ++i) shown_pixels_[i] = snapshot_.model.pixels[i];
  pixels_dirty_ = false;
  last_pixel_ms_ = now_ms;
}

// Dirty-driven: a new snapshot, a phase change or a wake requests a frame. The
// frame is composed from the core-local snapshot copy, then presented only when
// it differs from what the panel holds. Frames are capped at 30 Hz, and nothing
// is composed while the splash is up, before the first model, or while asleep.
void UiWorker::render(std::uint32_t now_ms) noexcept {
  if (!need_render_ || !have_model_ || !assets_ready_) return;
  if (power_.phase() != render::ScreenPhase::Active) return;
  if (elapsed(now_ms, last_render_ms_) < render::kFrameIntervalMs) return;
  last_render_ms_ = now_ms;
  need_render_ = false;
  const std::uint32_t compose_from = iteration_.mark_us;
  render::render_screen(snapshot_.model, assets_, frame_);
  const std::uint32_t t0 = clock_.now().us;
  iteration_.stage(Core1Phase::Compose, t0);
  stats_.compose.record(t0 - compose_from, now_ms);
  (void)platform::display_frame(frame_.bytes, render::kFrameBytes);
  const std::uint32_t t1 = clock_.now().us;
  stats_.render.record(t1 - t0, now_ms);
  iteration_.stage(Core1Phase::Present, t1);
}

void UiWorker::poll() noexcept {
  const TimeSample now = clock_.now();
  // Quiesce (scene operation or maintenance): acknowledge, then touch no peripheral until
  // the core-0 request resumes. Each pass is bounded, so the acknowledgement is prompt.
  const WorkerAction action = link_.maintenance.worker_poll();
  if (action == WorkerAction::Quiesce) {
    link_.maintenance.worker_acknowledge_quiescence();
    quiescent_ = true;
  } else if (action == WorkerAction::Resume) {
    quiescent_ = false;
    need_render_ = true;
  }
  if (quiescent_) {
    have_last_ = false;
    return;
  }
  // iteration_ still holds the previous body: attribute it to this period sample.
  if (have_last_) {
    const std::uint32_t period = now.us - last_start_us_;
    stats_.period.record(period, now.ms);
    stats_.attribution.note(period, now.us - last_end_us_, now.ms, iteration_.phase);
  }
  last_start_us_ = now.us;
  have_last_ = true;
  ++stats_.loops;
  iteration_.begin(now.us);

  // Physical scan -> ordered bridge events (switch press precedes key edges so
  // shortcuts see the modifier; switch release follows them).
  platform::GpioSample sample;
  const bool any = gpio_.scan(sample);
  const std::int16_t delta = gpio_.take_encoder_delta();
  std::uint32_t emitted = 0;
  if (sample.switch_pressed) {
    (void)emit(InputKind::EncoderSwitch, 0, 1, now.ms, sample.key_mask);
    ++emitted;
  }
  for (std::uint8_t n = 0; n < sample.edge_count; ++n) {
    const KeyEvent& edge = sample.edges[n];
    (void)emit(InputKind::KeyEdge, edge.key, edge.down ? 1 : 0, edge.at_ms, sample.key_mask);
    ++emitted;
  }
  if (delta != 0) {
    (void)emit(InputKind::EncoderDelta, 0, delta, now.ms, sample.key_mask);
    ++emitted;
  }
  if (sample.switch_released) {
    (void)emit(InputKind::EncoderSwitch, 0, 0, now.ms, sample.key_mask);
    ++emitted;
  }
  if (emitted > stats_.hi_events_per_pass) stats_.hi_events_per_pass = emitted;
  const std::uint32_t depth = static_cast<std::uint32_t>(link_.bridge.queue_depth());
  if (depth > stats_.hi_bridge_depth) stats_.hi_bridge_depth = depth;

  // Recovery: core 0 changed epoch or acknowledged an overflow fault. Send an
  // authoritative all-keys-up Resync as soon as the pad is physically idle.
  const std::uint32_t epoch = link_.bridge.requested_epoch();
  const std::uint32_t fault = link_.bridge.state().fault_generation;
  if (epoch != seen_epoch_ || fault != seen_fault_) {
    seen_epoch_ = epoch;
    seen_fault_ = fault;
    resync_pending_ = true;
  }
  if (resync_pending_ && sample.key_mask == 0 &&
      emit(InputKind::Resync, 0, 0, now.ms, sample.key_mask)) {
    resync_pending_ = false;
    ++stats_.resyncs;
  }
  const std::uint32_t t_scan = clock_.now().us;
  stats_.scan.record(t_scan - now.us, now.ms);
  iteration_.stage(Core1Phase::Scan, t_scan);
  stats_.key_mask = sample.key_mask;

  // Sleep / wake on raw physical activity (matches the T00 spike and Orion.py).
  if (any || delta != 0) {
    if (power_.activity(now.ms)) {  // wake: power the panel and redraw
      platform::display_power(true);
      need_render_ = true;
    }
  }
  if (power_.tick(now.ms)) platform::display_power(false);  // idle > 60 s
  if (power_.phase() != last_phase_) {
    if (power_.phase() == render::ScreenPhase::Active) need_render_ = true;  // splash ended
    last_phase_ = power_.phase();
  }
  stats_.sleeping = power_.phase() == render::ScreenPhase::Asleep;
  iteration_.stage(Core1Phase::Power, clock_.now().us);

  // Snapshots from core 0 (newest wins).
  const std::uint32_t ui_depth = static_cast<std::uint32_t>(link_.ui.size());
  if (ui_depth > stats_.hi_ui_depth) stats_.hi_ui_depth = ui_depth;
  const std::size_t drained = ui_depth != 0 ? link_.ui.drain_newest(snapshot_) : 0U;
  if (drained != 0) {
    stats_.snapshots_drained += static_cast<std::uint32_t>(drained);
    have_model_ = true;
    need_render_ = true;
    stats_.have_model = true;
    for (std::size_t i = 0; i < kKeys; ++i) {
      const RgbColor& a = snapshot_.model.pixels[i];
      const RgbColor& b = shown_pixels_[i];
      if (a.red != b.red || a.green != b.green || a.blue != b.blue) pixels_dirty_ = true;
    }
  }
  if (link_.diag.size() != 0 && link_.diag.drain_newest(diag_) != 0) have_diag_ = true;
  iteration_.stage(Core1Phase::Model, clock_.now().us);

  apply_pixels(now.ms);
  iteration_.stage(Core1Phase::Pixels, clock_.now().us);
  render(now.ms);

  if (elapsed(now.ms, last_stack_ms_) >= 500U) {
    last_stack_ms_ = now.ms;
    stats_.stack = platform::stack_report(1);
    stats_.encoder_glitches = gpio_.encoder().glitches();
  }
  // The first pass publishes at once: the core-0 gesture decides from that first pad sample.
  if (stats_.loops == 1U || elapsed(now.ms, last_stats_ms_) >= 250U) {
    last_stats_ms_ = now.ms;
    (void)link_.core1.try_push(stats_);
  }
  // Tail ends the body: everything after the last phase, including the publication above.
  last_end_us_ = clock_.now().us;
  iteration_.stage(Core1Phase::Tail, last_end_us_);
}

}  // namespace orbit
