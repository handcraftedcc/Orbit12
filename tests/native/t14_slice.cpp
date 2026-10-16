// Headless-slice composition (app_main) and T14 application integration.
// Timing statistics, bounded text, a single-threaded host run of EngineApp + UiWorker
// over stub platform adapters, and an in-memory storage world with a write-window guard.
// Verifies behavior (key -> note-on/off, release safety, clock error accounting, sleep/wake,
// serial dump framing, boot load, in-place scene switch, failed switch with recovery,
// maintenance entry/exit with notes released), not implementation.
#include "modules/generated/module_counts_generated.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app/app_main.hpp"
#include "app/build_info.hpp"
#include "modules/common/registry.hpp"
#include "t13_fs.hpp"

namespace {
int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

// ---- Stub platform world ----
std::uint64_t g_us = 1000;
std::vector<orbit::MidiPacket> g_sent;
orbit::platform::GpioSample g_next_sample;
std::int16_t g_next_delta = 0;
bool g_display_on = true;
std::uint32_t g_presents = 0, g_pixel_shows = 0;
orbit::RgbColor g_last_pixels[orbit::kKeys];
std::string g_serial_in, g_serial_out;
// Bytes the CDC port accepts before the host must drain it (64 per loop pass; time
// advances by 100 us when a writer has to wait, so bounded waits terminate).
std::size_t g_serial_room = 64;
// True while the simulated host does not read: writers wait for the bound, then fail.
bool g_host_stalled = false;
// Debounced pad state as the GPIO adapter reports it: edges update it, scans repeat it.
std::uint16_t g_key_mask = 0;
}  // namespace

namespace orbit {
namespace platform {
TimeSample sample_time() noexcept { return {static_cast<std::uint32_t>(g_us / 1000U), static_cast<std::uint32_t>(g_us)}; }
namespace {
TimeSample clock_sample(void*) { return sample_time(); }
}  // namespace
ClockPort clock_port() noexcept { return {nullptr, &clock_sample}; }

void Encoder::begin(bool) noexcept {}
void Encoder::service() noexcept {}
void GpioInput::begin(bool) noexcept {}
bool GpioInput::scan(GpioSample& out) noexcept {
  out = g_next_sample;
  g_next_sample = GpioSample{};
  for (std::uint8_t n = 0; n < out.edge_count; ++n) {
    const auto bit = static_cast<std::uint16_t>(1U << out.edges[n].key);
    g_key_mask = out.edges[n].down ? static_cast<std::uint16_t>(g_key_mask | bit)
                                   : static_cast<std::uint16_t>(g_key_mask & ~bit);
  }
  out.key_mask = g_key_mask;
  encoder_.service();
  return out.edge_count != 0 || out.switch_pressed || out.switch_released;
}
bool GpioInput::poll_frame(InputFrame&) noexcept { return false; }
InputPort GpioInput::input_port() noexcept { return {}; }

namespace {
bool send_thunk(void* ctx, const MidiPacket& packet) { return static_cast<UsbMidi*>(ctx)->try_send(packet); }
}  // namespace
bool UsbMidi::begin(bool) noexcept { return true; }
void UsbMidi::poll() noexcept {}
bool UsbMidi::connected() const noexcept { return true; }
MidiPort UsbMidi::port() noexcept { return {this, &send_thunk}; }
bool UsbMidi::try_send(const MidiPacket& packet) noexcept { g_sent.push_back(packet); return true; }
UsbMidiDiagnostics UsbMidi::diagnostics() const noexcept { return diag_; }

void display_begin() noexcept {}
void display_splash() noexcept {}
void display_present(const TextScreen&) noexcept { ++g_presents; }
bool display_frame(const std::uint8_t*, std::size_t) noexcept { ++g_presents; return true; }
void display_power(bool on) noexcept { g_display_on = on; }
void pixels_begin() noexcept {}
void pixels_show(const RgbColor* colors, std::size_t count) noexcept {
  ++g_pixel_shows;
  for (std::size_t i = 0; i < count && i < kKeys; ++i) g_last_pixels[i] = colors[i];
}
void serial_begin() noexcept {}
int serial_read() noexcept {
  if (g_serial_in.empty()) return -1;
  const int c = static_cast<unsigned char>(g_serial_in.front());
  g_serial_in.erase(0, 1);
  return c;
}
std::size_t serial_writable() noexcept {
  if (g_serial_room == 0) {  // the writer waits: time passes, and a live host drains
    g_us += 100;
    if (!g_host_stalled) g_serial_room = 64;
  }
  return g_serial_room;
}
std::size_t serial_write(const char* data, std::size_t length) noexcept {
  const std::size_t n = length < g_serial_room ? length : g_serial_room;
  g_serial_room -= n;
  g_serial_out.append(data, n);
  return n;
}
bool serial_connected() noexcept { return true; }
void stack_paint(std::uint8_t) noexcept {}
StackReport stack_report(std::uint8_t) noexcept { return {}; }
HeapReport heap_report() noexcept { return {}; }
}  // namespace platform
}  // namespace orbit

using namespace orbit;

namespace {

// ---- Storage world: the shared in-memory LittleFS with a guarded write window ----
bool g_mounted = true;
// Unmounted partition: every port operation fails until FORMAT succeeds (as the adapter does).
bool g_port_dead = false;
bool g_window_open = false;
int g_window_changes = 0;
int g_unguarded_writes = 0;

bool fake_mount(void*) noexcept { return g_mounted; }
bool fake_mounted(void*) noexcept { return g_mounted; }
fsformat::FormatResult fake_format(void*) noexcept {
  // Stands for the verified format: on success the partition is mounted and usable again.
  g_port_dead = false;
  g_mounted = true;
  return fsformat::FormatResult::Ok;
}
SceneStorageStatus dead_exists(void* context, SceneFileId id, bool& exists) {
  if (g_port_dead) { exists = false; return SceneStorageStatus::Error; }
  return t13::exists_op(context, id, exists);
}
SceneStorageStatus dead_open_read(void* context, SceneFileId id, SceneReader& reader) {
  if (g_port_dead) return SceneStorageStatus::Error;
  return t13::open_read_op(context, id, reader);
}
SceneStorageStatus dead_close(void* context, SceneFileId id) {
  if (g_port_dead) return SceneStorageStatus::Error;
  return t13::close_op(context, id);
}
void fake_window(void*, bool open) noexcept {
  g_window_open = open;
  ++g_window_changes;
}
SceneStorageStatus guarded_open_write(void* context, SceneFileId id, SceneWriter& writer) {
  if (g_port_dead) return SceneStorageStatus::Error;
  if (!g_window_open) { ++g_unguarded_writes; return SceneStorageStatus::Error; }
  return t13::open_write_op(context, id, writer);
}
SceneStorageStatus guarded_rename(void* context, SceneFileId from, SceneFileId to) {
  if (g_port_dead) return SceneStorageStatus::Error;
  if (!g_window_open) { ++g_unguarded_writes; return SceneStorageStatus::Error; }
  return t13::rename_op(context, from, to);
}
SceneStorageStatus guarded_remove(void* context, SceneFileId id) {
  if (g_port_dead) return SceneStorageStatus::Error;
  if (!g_window_open) { ++g_unguarded_writes; return SceneStorageStatus::Error; }
  return t13::remove_op(context, id);
}
AppStorage app_storage() noexcept {
  AppStorage storage;
  storage.context = &t13::shared_fs();
  storage.mount = &fake_mount;
  storage.set_window = &fake_window;
  storage.format = &fake_format;
  storage.mounted = &fake_mounted;
  storage.port = t13::port(t13::shared_fs());
  storage.port.exists = &dead_exists;
  storage.port.open_read = &dead_open_read;
  storage.port.close = &dead_close;
  storage.port.open_write = &guarded_open_write;
  storage.port.rename = &guarded_rename;
  storage.port.remove = &guarded_remove;
  return storage;
}

SceneFileId scene_id(std::uint8_t scene) noexcept { return {SceneFileKind::Scene, scene}; }
SceneFileId user_settings_id() noexcept { return {SceneFileKind::UserSettings, 0}; }
SceneFileId pointer_id() noexcept { return {SceneFileKind::ActivePointer, 0}; }
SceneFileId names_id() noexcept { return {SceneFileKind::SceneNames, 0}; }
// Named slots 1..10 as a booted device finds them. The tests address slots 0..9.
constexpr char kNamesText[] =
    "01,ALFA\n02,BRAVO\n03,CHARLIE\n04,DELTA\n05,ECHO\n06,FOXTROT\n07,GOLF\n08,HOTEL\n09,INDIA\n10,JULIET\n";
void put_names() { t13::shared_fs().put(names_id(), kNamesText); }

// The stock SCN1 text (oracle scene_default) with the SET key changed to `key`.
std::string scene_with_key(int key) {
  std::string text =
      "SCN1\nS,key,0\nS,scale,1\nS,octave,3\nS,key_offset,0\nS,bpm,120\nS,swing,0.0\n"
      "S,transport_mode,0\nM,0,set,1\nP,key,0\nP,scale,1\nP,bpm,120\nP,swing,0.0\n"
      "P,transport_mode,0\nM,1,note,1\nP,octave,3\nP,key_offset,0\nP,velocity,127\n"
      "P,layout,0\nP,operation_mode,0\nP,out_channel,1\nM,2,empty,1\nM,3,empty,1\n"
      "M,4,empty,1\nM,5,empty,1\nM,6,empty,1\nM,7,empty,1\nM,8,output,1\nP,out_ch,1\n"
      "P,print,0\nM,9,settings,1\nEND\n";
  const std::string digit = std::to_string(key);
  std::size_t at = text.find("S,key,0\n");
  text.replace(at + 6, 1, digit);
  at = text.find("P,key,0\n");
  text.replace(at + 6, 1, digit);
  return text;
}

struct Rig {
  SharedLink link;
  platform::UsbMidi usb;
  platform::GpioInput gpio;
  EngineApp engine;
  UiWorker ui;
  // Resets the simulated world unless keep_files: tests that preset storage pass keep_files,
  // then boot through settle().
  explicit Rig(bool settle_now = true, bool keep_files = false, std::uint16_t boot_keys = 0,
               bool mount_ok = true)
      : engine(link, platform::clock_port(), usb, app_storage()),
        ui(link, platform::clock_port(), gpio) {
    g_sent.clear();
    g_serial_out.clear();
    g_serial_in.clear();
    g_serial_room = 64;
    g_us = 1000;
    g_display_on = true;
    g_presents = g_pixel_shows = 0;
    g_next_sample = platform::GpioSample{};
    g_key_mask = boot_keys;  // pads already down at power-up: state only, no edges
    g_host_stalled = false;
    g_mounted = mount_ok;
    g_port_dead = !mount_ok;
    g_window_open = false;
    g_window_changes = 0;
    g_unguarded_writes = 0;
    if (!keep_files) {
      t13::shared_fs().reset();
      put_names();
    }
    engine.setup();
    ui.setup();
    if (settle_now) settle();
  }
  // One scheduling slice of 100 us on each core, as the two loops interleave.
  void slice() {
    g_us += 100;
    if (!g_host_stalled) g_serial_room = 64;
    ui.poll();
    engine.poll();
  }
  void run(std::uint32_t slices) {
    for (std::uint32_t i = 0; i < slices; ++i) slice();
  }
  // Runs until boot (KEYBRI and the active scene) has completed and no scene operation runs.
  void settle() {
    std::uint32_t i = 0;
    while (!(engine.boot_done() && !engine.controller().scene_busy())) {
      if (++i > 20000) { CHECK(false); return; }  // boot never completed
      slice();
    }
    // The boot epoch change is applied by the next controller pass and then needs a
    // physical all-keys-up resync before musical input: run until both are done.
    for (std::uint32_t n = 0; n < 20; ++n) slice();
    for (std::uint32_t n = 0; n < 50 && link.bridge.awaiting_resync(); ++n) slice();
    if (g_key_mask == 0) CHECK(!link.bridge.awaiting_resync());
  }
  bool run_until_idle(std::uint32_t max_slices = 20000) {
    for (std::uint32_t i = 0; i < max_slices; ++i) {
      if (!engine.controller().scene_busy()) return true;
      slice();
    }
    return false;
  }
  void key(std::uint8_t key, bool down) {
    g_next_sample = platform::GpioSample{};
    g_next_sample.edges[0] = KeyEvent{key, down, static_cast<std::uint32_t>(g_us / 1000U)};
    g_next_sample.edge_count = 1;
  }
  void encoder(bool down) {
    g_next_sample = platform::GpioSample{};
    g_next_sample.switch_pressed = down;
    g_next_sample.switch_released = !down;
    g_next_sample.switch_down = down;
  }
  std::size_t count(MidiKind kind, int pitch = -1) const {
    std::size_t n = 0;
    for (const auto& p : g_sent) if (p.kind == kind && (pitch < 0 || p.data1 == pitch)) ++n;
    return n;
  }
  const Controller& ctl() const { return engine.controller(); }
};

void latency_stats() {
  LatencyStats s;
  CHECK(s.p99_us() == 0 && s.avg_us() == 0);
  for (int i = 0; i < 200; ++i) s.record(40, 0);
  CHECK(s.count == 200 && s.max_us == 40 && s.avg_us() == 40);
  CHECK(s.p99_us() == 40);                 // bucket bound 64 is clamped to the observed max
  s.record(5000, 1);
  s.record(5000, 1);
  CHECK(s.max_us == 5000);
  CHECK(s.p99_us() >= 40 && s.p99_us() <= 5000);
  // 10 s window rolls: the previous window max is retained, the new one restarts.
  s.record(7, 20000);
  CHECK(s.last_win_max_us == 5000 && s.win_max_us == 7);
  LatencyStats few;
  few.record(900, 0);
  CHECK(few.p99_us() == 900);              // < 100 samples reports the maximum
  CHECK(LatencyStats::bucket_for(0) == 0 && LatencyStats::bucket_for(16) == 0 && LatencyStats::bucket_for(17) == 1);
  CHECK(LatencyStats::bucket_for(0xffffffffU) == LatencyStats::kBuckets - 1U);
}

void text_builders() {
  char small[8];
  TextOut o(small, sizeof small);
  o.s("abcdefghij");
  CHECK(o.overflowed() && o.size() == 7 && small[7] == '\0');
  char buf[64];
  TextOut n(buf, sizeof buf);
  n.u(0).c(' ').u(4294967295U).c(' ').i(-12).c(' ').i(-2147483647 - 1);
  CHECK(std::strcmp(buf, "0 4294967295 -12 -2147483648") == 0);
  char line[platform::kScreenColumns + 1];
  ScreenLine l(line);
  l.s("this text is far too long for one line");
  l.finish();
  CHECK(std::strlen(line) == platform::kScreenColumns);
  ScreenLine p(line);
  p.u(7, 3).c('|');
  p.finish();
  CHECK(std::strncmp(line, "007|", 4) == 0 && std::strlen(line) == platform::kScreenColumns);
}

// A serial dump that does not fit must end with a visible TRUNCATED line, never a silent cut.
void dump_close_reports_truncation() {
  constexpr std::size_t kCap = 256;
  char fits[kCap];
  TextOut body(fits, kCap - kDumpTailBytes);
  body.s("A\n");
  const std::size_t fit_len = close_dump(body, fits, kCap, 0);
  CHECK(!body.overflowed() && std::string(fits, fit_len) == "A\nEND\n");

  char cut[kCap];
  TextOut long_body(cut, kCap - kDumpTailBytes);
  long_body.s(std::string(400, 'x').c_str());
  CHECK(long_body.overflowed());
  const std::size_t cut_len = close_dump(long_body, cut, kCap, 3);
  const std::string out(cut, cut_len);
  const std::string tail = "\nTRUNCATED dumps=3\nEND\n";
  CHECK(cut_len <= kCap);
  CHECK(out.size() >= tail.size() && out.compare(out.size() - tail.size(), tail.size(), tail) == 0);
}

void key_to_midi_and_clock_accounting() {
  auto r = std::make_unique<Rig>();
  r->run(20);
  r->key(9, true);                         // input id 9 -> MIDI 60 in C major octave 3
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn, 60) == 1);
  CHECK(r->count(MidiKind::Start) == 1);   // first note starts the internal clock
  CHECK(r->engine.controller().output().is_sounding(0, 60));
  r->run(300);                             // 30 ms at 120 BPM = ~ 2.4 ticks of 20.8 ms
  CHECK(r->count(MidiKind::Clock) >= 2);
  r->key(9, false);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOff, 60) == 1);
  CHECK(r->engine.controller().output().held_count() == 0);
}

void snapshots_feed_pixels_and_screen() {
  auto r = std::make_unique<Rig>();
  r->run(20000);                           // 2 s: past the 1.5 s splash
  CHECK(g_pixel_shows >= 1);
  CHECK(g_presents >= 1);
}

void sleep_and_wake() {
  auto r = std::make_unique<Rig>();
  r->run(100);
  CHECK(g_display_on);
  for (int i = 0; i < 70; ++i) {           // 70 s idle in 1 ms slices
    g_us += 1000000;
    r->ui.poll();
    r->engine.poll();
  }
  CHECK(!g_display_on);
  r->key(3, true);
  r->run(3);
  CHECK(g_display_on);                     // any input wakes
  r->key(3, false);
  r->run(3);
}

void serial_dump_is_framed_and_read_only() {
  auto r = std::make_unique<Rig>();
  r->run(3000);
  g_serial_in = "x?";
  const std::size_t sent_before = g_sent.size();
  r->run(200);                             // 64 bytes per pass drains the dump
  CHECK(g_serial_out.rfind("ORBIT12-SLICE", 0) == 0);
  CHECK(g_serial_out.size() > 800 && g_serial_out.size() < 4096);  // within kDumpCapacity
  CHECK(g_serial_out.compare(g_serial_out.size() - 4, 4, "END\n") == 0);
  CHECK(g_serial_out.find("C0_PERIOD_US") != std::string::npos);
  CHECK(g_serial_out.find("TICK_LATE_US") != std::string::npos);
  CHECK(g_serial_out.find("C1_PERIOD_US") != std::string::npos);
  CHECK(g_serial_out.find("STORAGE mounted=1") != std::string::npos);
  // Build identity line: wiring from app/build_info.hpp (the committed placeholder in host builds).
  const std::string version_line = std::string("VERSION fw=") + orbit::build::kFirmwareVersion +
      " git=" + orbit::build::kGitSha + " engine_api=" + std::to_string(orbit::build::kEngineApi) +
      " board=" + orbit::build::kBoardId;
  CHECK(g_serial_out.find(version_line) != std::string::npos);
  CHECK(g_serial_out.find("MAINT state=0") != std::string::npos);
  CHECK(g_sent.size() == sent_before);     // the dump never touches the MIDI path
  g_serial_out.clear();
  g_serial_in = "?";
  r->run(200);
  CHECK(!g_serial_out.empty());            // repeatable
}

void bridge_events_ordered_with_encoder() {
  auto r = std::make_unique<Rig>();
  r->run(10);
  g_next_sample = platform::GpioSample{};
  g_next_sample.switch_pressed = true;
  g_next_sample.switch_down = true;
  r->run(2);
  CHECK(r->engine.controller().encoder_down());
  g_next_sample = platform::GpioSample{};
  g_next_sample.switch_released = true;
  r->run(2);
  CHECK(!r->engine.controller().encoder_down());
}

// ---- T14 integration ----

// First boot (no names file): ALFA is created in slot 0 from the stock defaults, once.
void first_boot_creates_alfa_once() {
  t13::shared_fs().reset();                // empty filesystem: no names, no scene, no pointer
  auto r = std::make_unique<Rig>(true, true);
  const Controller& c = r->ctl();
  CHECK(c.boot_complete());
  CHECK(c.active_scene() == 0);
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.scene_service().slot_occupied(0));
  CHECK(std::strcmp(c.scene_service().slot_name(0), "ALFA") == 0);
  CHECK(c.scene_service().occupied_count() == 1);
  CHECK(c.diagnostics().scene_failures == 0);
  CHECK(c.diagnostics().boot_default_fallbacks == 0);
  CHECK(c.global().key == 0 && c.global().scale == 1);
  CHECK(std::strcmp(c.slot_module_id(kSlotIn), "note") == 0);
  CHECK(std::strcmp(c.slot_module_id(kSlotP1), "empty") == 0);
  CHECK(std::fabs(c.key_brightness() - 1.0f) < 1e-6f);
  CHECK(t13::shared_fs().cell(scene_id(0)).exists);                // the one first-run write set
  CHECK(t13::shared_fs().equals(names_id(), "01,ALFA\n"));
  CHECK(t13::shared_fs().equals(pointer_id(), "1\n"));
  CHECK(!t13::shared_fs().cell(user_settings_id()).exists);         // settings are not written at boot
  CHECK(g_unguarded_writes == 0);
  CHECK(g_window_changes == 0 || !g_window_open);                   // window left closed
}

void boot_with_scene_loads_active_scene() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  t13::shared_fs().put(user_settings_id(), "key_brightness,0.5\n");
  auto r = std::make_unique<Rig>(false, true);   // preset files, then boot
  r->settle();
  const Controller& c = r->ctl();
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.global().key == 5);              // scene applied in place
  CHECK(c.active_scene() == 0);
  CHECK(std::fabs(c.key_brightness() - 0.5f) < 1e-6f);   // KEYBRI applied at boot
  CHECK(t13::shared_fs().mutations == 0);  // boot writes nothing
  CHECK(g_unguarded_writes == 0);
}

void corrupt_scene_falls_back_to_defaults() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), "SCN1\nthis is not a scene\n");
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  const Controller& c = r->ctl();
  CHECK(c.last_scene_status() == SceneServiceStatus::SceneError);
  CHECK(c.diagnostics().scene_failures == 1);       // a corrupt file is a real failure
  CHECK(c.diagnostics().boot_default_fallbacks == 1);
  CHECK(c.global().key == 0);                       // stock defaults, never the bad text
  CHECK(std::strcmp(c.slot_module_id(kSlotIn), "note") == 0);
  CHECK(t13::shared_fs().equals(scene_id(0), "SCN1\nthis is not a scene\n"));  // not overwritten
  CHECK(t13::shared_fs().mutations == 0);
  CHECK(r->engine.storage_mounted());
}

void scene_switch_is_in_place() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  t13::shared_fs().put(scene_id(1), scene_with_key(2).c_str());
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  CHECK(r->ctl().global().key == 5);
  const std::uint32_t epoch_before = r->ctl().epoch();
  CHECK(r->engine.controller().request_scene_switch(1));
  CHECK(r->run_until_idle());
  r->run(10);                               // the controller applies the epoch change on its next pass
  const Controller& c = r->ctl();
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.active_scene() == 1);
  CHECK(c.global().key == 2);
  CHECK(c.epoch() > epoch_before);                  // physical resync after the transition
  CHECK(t13::shared_fs().equals(pointer_id(), "2\n"));
  CHECK(g_unguarded_writes == 0);
  CHECK(!g_window_open);                            // window closed on resume
  CHECK(g_window_changes >= 2);
  // The same engine keeps running: a key after the resync sounds on the new scene.
  r->key(9, false);
  r->run(30);
  const std::size_t before = r->count(MidiKind::NoteOn);
  r->key(9, true);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn) == before + 1);  // performance input works on the new scene
  r->key(9, false);
  r->run(5);
}

void failed_switch_then_gesture_recovers_in_place() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  t13::shared_fs().put(scene_id(1), scene_with_key(2).c_str());
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  CHECK(r->ctl().global().key == 5);
  CHECK(r->engine.controller().request_scene_switch(1));
  // Cut power exactly when the pointer is about to commit: the rollback read also fails.
  for (std::uint32_t i = 0; i < 20000 && r->ctl().scene_service().phase() != SceneServicePhase::CommitPointer; ++i)
    r->slice();
  CHECK(r->ctl().scene_service().phase() == SceneServicePhase::CommitPointer);
  t13::shared_fs().powered_off = true;
  for (std::uint32_t i = 0; i < 20000 && !r->ctl().scene_failed(); ++i) r->slice();
  CHECK(r->ctl().scene_failed());
  CHECK(r->ctl().diagnostics().scene_failures == 1);
  // Failed: the quiesce is released, pads show the failure, and musical keys are dropped.
  r->run(400);
  CHECK(g_last_pixels[0].red >= 200 && g_last_pixels[0].green == 0 && g_last_pixels[0].blue == 0);
  const std::size_t before_failed = r->count(MidiKind::NoteOn);
  r->key(9, true);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn) == before_failed);
  r->key(9, false);
  r->run(5);
  // USER-REQUESTED TOGGLE (known-quirks.md): keys 3/6 cycle the chain shortcuts, but Failed
  // drops every non-encoder event before the shortcut table, so neither key changes the UI.
  {
    const std::uint8_t chain_failed = r->ctl().active_chain();
    const UiSection section_failed = r->ctl().section();
    r->key(2, true); r->run(5); r->key(2, false); r->run(5);
    r->key(5, true); r->run(5); r->key(5, false); r->run(5);
    CHECK(r->ctl().scene_failed());
    CHECK(r->ctl().active_chain() == chain_failed && r->ctl().section() == section_failed);
  }
  // Power comes back; the encoder press is the recovery gesture.
  t13::shared_fs().reboot();
  r->encoder(true);
  r->run(2);
  r->encoder(false);
  CHECK(r->run_until_idle());
  const Controller& c = r->ctl();
  CHECK(!c.scene_failed());
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.active_scene() == 0);
  CHECK(c.global().key == 5);                       // committed scene restored in place
  CHECK(c.diagnostics().scene_failures == 1);       // recovery success is not a new failure
  CHECK(g_unguarded_writes == 0);
  r->run(400);
  CHECK(!(g_last_pixels[0].red == 255 && g_last_pixels[0].green == 0 && g_last_pixels[0].blue == 0));
  const std::size_t before_resume = r->count(MidiKind::NoteOn);
  r->key(9, true);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn) == before_resume + 1);  // events flow again after the resync
  r->key(9, false);
  r->run(5);
}

void failed_recovery_counts_each_failure() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  t13::shared_fs().put(scene_id(1), scene_with_key(2).c_str());
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  CHECK(r->engine.controller().request_scene_switch(1));
  for (std::uint32_t i = 0; i < 20000 && r->ctl().scene_service().phase() != SceneServicePhase::CommitPointer; ++i)
    r->slice();
  t13::shared_fs().powered_off = true;
  for (std::uint32_t i = 0; i < 20000 && !r->ctl().scene_failed(); ++i) r->slice();
  CHECK(r->ctl().diagnostics().scene_failures == 1);
  // Recovery attempted while the storage is still down fails again: one more failure.
  r->encoder(true);
  r->run(2);
  r->encoder(false);
  for (std::uint32_t i = 0; i < 20000 && r->ctl().scene_service().phase() == SceneServicePhase::RecoveryQuiesce; ++i)
    r->slice();
  for (std::uint32_t i = 0; i < 20000 && !r->ctl().scene_failed(); ++i) r->slice();
  for (std::uint32_t i = 0; i < 200; ++i) r->slice();
  CHECK(r->ctl().scene_failed());
  CHECK(r->ctl().diagnostics().scene_failures == 2);
  t13::shared_fs().reboot();
  r->encoder(true);
  r->run(2);
  r->encoder(false);
  CHECK(r->run_until_idle());
  CHECK(!r->ctl().scene_failed());
  CHECK(r->ctl().diagnostics().scene_failures == 2);
  CHECK(r->ctl().global().key == 5);
}

// ---- Scene gesture flow (SETTINGS rows, encoder and keys as on the device) ----

// Encoder rotation enters the controller as the bridge delivers it. The test rig's GPIO stub
// cannot inject quadrature, so the decoded delta is handed to the controller directly.
void turn(Rig& r, int delta) {
  r.run_until_idle();
  PhysicalEvent e{};
  e.kind = InputKind::EncoderDelta;
  e.value = static_cast<std::int16_t>(delta);
  e.at_ms = static_cast<std::uint32_t>(g_us / 1000U);
  r.engine.controller().handle_event(e);
  r.run(2);
}
// Encoder press and release through the GPIO path (the same events as the device).
void click(Rig& r) {
  r.run_until_idle();
  r.encoder(true);
  r.run(3);
  r.encoder(false);
  r.run(3);
  r.run_until_idle();
}
// Encoder held plus key 5 (the chain-view shortcut) returns to the chain view.
void to_chain_view(Rig& r) {
  r.run_until_idle();
  r.encoder(true);
  r.run(3);
  r.key(4, true);
  r.run(3);
  r.key(4, false);
  r.run(3);
  r.encoder(false);
  r.run(3);
  r.run_until_idle();
}
void goto_slot(Rig& r, std::uint8_t slot) {
  to_chain_view(r);
  for (std::uint8_t i = 0; i < kSlotCount && r.ctl().active_chain() != slot; ++i) turn(r, 1);
  CHECK(r.ctl().active_chain() == slot);
}
bool file_contains(orbit::SceneFileId id, const char* text) {
  auto& file = t13::shared_fs().cell(id);
  const std::string body(reinterpret_cast<const char*>(file.bytes), file.size);
  return file.exists && body.find(text) != std::string::npos;
}

// A fresh device (empty filesystem, mounted). The user edits SET key in scene 1, saves it from
// SETTINGS, switches to scene 2 (never saved: defaults), edits and saves it, then switches back.
void empty_fs_gesture_save_switch_round_trip() {
  auto r = std::make_unique<Rig>();
  Controller& c = r->engine.controller();
  CHECK(c.active_scene() == 0 && c.global().key == 0);
  // 1. Edit SET key (row 0) in scene 1: 0 -> 2.
  goto_slot(*r, kSlotSet);
  CHECK(std::strcmp(c.slot(kSlotSet).rows[0].name, "key") == 0);
  click(*r);                                         // ParmSelection
  click(*r);                                         // ParmEdit on key
  turn(*r, 2);
  click(*r);                                         // exit to ParmSelection
  CHECK(c.global().key == 2);
  // 2. SAVSCN (row 1 of SETTINGS): saves scene 1 and nothing else.
  goto_slot(*r, kSlotSettings);
  click(*r);                                         // ParmSelection, row 0 SCENE
  turn(*r, 1);                                       // row 1 SAVSCN
  click(*r);                                         // enter: save scene 1
  CHECK(r->run_until_idle());
  CHECK(c.diagnostics().scene_failures == 0);
  CHECK(file_contains(scene_id(0), "S,key,2\n"));
  CHECK(!t13::shared_fs().cell(scene_id(1)).exists);
  CHECK(!t13::shared_fs().cell(pointer_id()).exists);  // a save never moves the pointer
  click(*r);                                         // exit SAVSCN
  // 3. SCENE row: 0 -> 1 (scene 2 does not exist). Exit switches.
  turn(*r, -1);                                      // row 0 SCENE
  click(*r);                                         // ParmEdit on SCENE
  turn(*r, 1);
  CHECK(c.slot(kSlotSettings).rows[0].value_i == 1);
  click(*r);                                         // exit: switch to scene 2
  CHECK(r->run_until_idle());
  r->run(20);
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.active_scene() == 1);
  CHECK(c.global().key == 0);                        // stock defaults for a missing scene
  CHECK(c.slot(kSlotSettings).rows[0].value_i == 1);
  CHECK(c.diagnostics().scene_failures == 0 && c.diagnostics().scene_requests_ignored == 0);
  CHECK(t13::shared_fs().equals(pointer_id(), "2\n"));
  CHECK(file_contains(scene_id(1), "S,key,0\n"));    // defaults saved to scene 2
  CHECK(file_contains(scene_id(0), "S,key,2\n"));    // scene 1 untouched by the switch
  CHECK(g_unguarded_writes == 0);
  // 4. Edit SET key in scene 2 (0 -> 5), save it.
  goto_slot(*r, kSlotSet);
  click(*r);
  click(*r);
  turn(*r, 5);
  click(*r);
  CHECK(c.global().key == 5);
  goto_slot(*r, kSlotSettings);
  click(*r);
  turn(*r, 1);
  click(*r);
  CHECK(r->run_until_idle());
  click(*r);
  CHECK(file_contains(scene_id(1), "S,key,5\n"));
  CHECK(file_contains(scene_id(0), "S,key,2\n"));    // saving scene 2 never writes scene 1
  // 5. Switch back to scene 1: its saved key comes back, scene 2 keeps its own.
  turn(*r, -1);
  click(*r);
  turn(*r, -1);
  click(*r);
  CHECK(r->run_until_idle());
  r->run(20);
  CHECK(c.active_scene() == 0);
  CHECK(c.last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(c.global().key == 2);
  CHECK(t13::shared_fs().equals(pointer_id(), "1\n"));
  CHECK(file_contains(scene_id(1), "S,key,5\n"));
  CHECK(c.output().held_count() == 0);               // no notes left sounding across the switches
  CHECK(c.diagnostics().scene_failures == 0);
  CHECK(g_unguarded_writes == 0);
}

// A corrupt target is not a missing one: the switch is refused before any live change, the
// index stays, and the SCENE row shows the active index again (not the requested one).
void corrupt_target_switch_is_refused_and_row_shows_active() {
  auto r = std::make_unique<Rig>();
  Controller& c = r->engine.controller();
  t13::shared_fs().put(scene_id(1), "SCN1\nthis is not a scene\n");
  goto_slot(*r, kSlotSettings);
  click(*r);                                         // ParmSelection, row 0 SCENE
  click(*r);                                         // ParmEdit
  turn(*r, 1);
  CHECK(c.slot(kSlotSettings).rows[0].value_i == 1);
  click(*r);                                         // exit: request the switch
  CHECK(r->run_until_idle());
  r->run(20);
  CHECK(c.last_scene_status() == SceneServiceStatus::SceneError);
  CHECK(c.active_scene() == 0);
  CHECK(c.slot(kSlotSettings).rows[0].value_i == 0);  // row follows the committed index
  CHECK(c.diagnostics().scene_failures == 1);
  CHECK(t13::shared_fs().equals(scene_id(1), "SCN1\nthis is not a scene\n"));  // not overwritten
  CHECK(!t13::shared_fs().cell(pointer_id()).exists);
  CHECK(g_unguarded_writes == 0);
}

// Builds a CDC request frame: sync 4F 31, type, name, payload (zero bytes here), CRC-32 (zlib).
std::string cdc_frame(char type, const std::string& name) {
  std::string frame = {'\x4F', '\x31', type, static_cast<char>(name.size()), '\0', '\0', '\0', '\0'};
  frame += name;
  std::uint32_t crc = maintenance::crc32_init();
  for (char ch : frame) crc = maintenance::crc32_update(crc, static_cast<std::uint8_t>(ch));
  crc = maintenance::crc32_final(crc);
  for (int shift = 0; shift < 32; shift += 8) frame += static_cast<char>((crc >> shift) & 0xFFU);
  return frame;
}

// A pad held at power-up (boot.py semantics) enters maintenance once it has been held 0.5 s.
// The note of a pad pressed after boot is released by the stop, and a LIST frame is served
// while the engine is silent. Exit with a 'Q' between frames, then play again in place.
void maintenance_entry_and_exit_release_notes() {
  auto r = std::make_unique<Rig>(true, false, static_cast<std::uint16_t>(1U << 9));  // pad 9 held at boot
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);   // not entered before the hold
  g_key_mask = 0;                           // pad 9 released early: no gesture, and the resync completes
  r->run(50);
  CHECK(!r->link.bridge.awaiting_resync());
  r->key(3, true);                          // a note sounds; the boot sample armed the gesture
  r->run(60);
  CHECK(r->count(MidiKind::NoteOn) == 1);
  r->run(10000);                            // pad 3 held: 500 ms after core 1 reports it, entry
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  CHECK(r->count(MidiKind::NoteOff) >= 1);  // released by the stop
  CHECK(r->engine.controller().output().held_count() == 0);
  CHECK(r->engine.controller().maintenance_hold());
  CHECK(g_window_open);                     // storage window opened after the quiesce
  const std::size_t sent_in_maintenance = g_sent.size();
  r->run(200);                              // core 1 is quiesced and core 0 is stopped: silence
  CHECK(g_sent.size() == sent_in_maintenance);

  g_serial_in = cdc_frame('L', "");         // LIST
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && static_cast<unsigned char>(g_serial_out[0]) == 0x4F &&
        static_cast<unsigned char>(g_serial_out[1]) == 0x31 && g_serial_out[2] == 'D');
  CHECK(r->engine.maintenance_session().counters().served == 1);
  CHECK(!r->engine.maintenance_session().transfer_open());

  // The physical release happens while core 1 is quiesced: model it as the mask only.
  g_key_mask = 0;
  g_serial_in += 'Q';
  r->run(200);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(!r->ctl().maintenance_hold());
  CHECK(!g_window_open);                    // closed on exit
  CHECK(g_unguarded_writes == 0);
  CHECK(r->count(MidiKind::NoteOn) == 1);   // no note-on was added by maintenance
  CHECK(r->count(MidiKind::NoteOff) == 1);  // the sounding note was released by the stop
  r->run(50);                               // resync after the exit
  const std::size_t before = r->count(MidiKind::NoteOn);
  r->key(3, true);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn) == before + 1);  // performance input works again in place
  r->key(3, false);
  r->run(5);
}

// Builds a CDC PUT frame with a payload (CRC over every byte before the trailer).
std::string cdc_put(const std::string& name, const std::string& payload) {
  const auto length = static_cast<std::uint32_t>(payload.size());
  std::string frame = {'\x4F', '\x31', 'P', static_cast<char>(name.size()),
                       static_cast<char>(length & 0xFFU), static_cast<char>((length >> 8) & 0xFFU),
                       static_cast<char>((length >> 16) & 0xFFU),
                       static_cast<char>((length >> 24) & 0xFFU)};
  frame += name;
  frame += payload;
  std::uint32_t crc = maintenance::crc32_init();
  for (char ch : frame) crc = maintenance::crc32_update(crc, static_cast<std::uint8_t>(ch));
  crc = maintenance::crc32_final(crc);
  for (int shift = 0; shift < 32; shift += 8) frame += static_cast<char>((crc >> shift) & 0xFFU);
  return frame;
}

// Reason byte of a one-byte-payload NAK response, or -1.
int nak_reason(const std::string& out) {
  if (out.size() < 9 || out[2] != 'N') return -1;
  const std::size_t at = 8 + static_cast<unsigned char>(out[3]);
  return at < out.size() ? static_cast<unsigned char>(out[at]) : -1;
}

// An unmounted partition (mount fails, every port operation fails) must not block boot, the
// maintenance gesture or the CDC pump. LIST and PUT are refused honestly while unmounted; a
// FORMAT with the wrong token changes nothing; the correct token ACKs after the (stand-in)
// verified format, the dump flag reports mounted, and the same port serves LIST and PUT afterwards.
void unmounted_boot_maintenance_format_recovers() {
  auto r = std::make_unique<Rig>(true, false, static_cast<std::uint16_t>(1U << 4), false);
  CHECK(r->engine.boot_done());                      // boot reaches Idle without storage
  CHECK(!r->ctl().scene_busy());
  CHECK(!r->ctl().scene_failed());                   // not Failed: defaults, no failure state
  CHECK(!r->engine.storage_mounted());
  CHECK(r->ctl().diagnostics().boot_default_fallbacks == 1);
  r->run(10000);                                     // pad 4 held from boot: gesture enters
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  CHECK(g_window_open);

  g_serial_out.clear();
  g_serial_in = cdc_frame('L', "");
  r->run(200);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::StorageError));

  g_serial_out.clear();
  g_serial_in = cdc_frame('F', "ERASE-FX");          // wrong token
  r->run(200);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::BadToken));
  CHECK(g_port_dead && !r->engine.storage_mounted());
  CHECK(r->engine.maintenance_session().counters().formats == 0);

  g_serial_out.clear();
  g_serial_in = cdc_frame('F', "ERASE-FS");          // correct token
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'A');
  CHECK(r->engine.storage_mounted());                // dump flag follows the adapter
  CHECK(r->engine.maintenance_session().counters().formats == 1);

  g_serial_out.clear();
  g_serial_in = cdc_frame('L', "");                  // same port now serves LIST
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'D');
  g_serial_out.clear();
  g_serial_in = cdc_put("user_settings", "key_brightness,0.5\n");
  r->run(300);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'A');
  CHECK(t13::shared_fs().equals(user_settings_id(), "key_brightness,0.5\n"));
  CHECK(g_unguarded_writes == 0);

  g_serial_in += 'Q';
  r->run(200);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(!g_window_open);
}

// A host that stops reading must not cost 2 s per chunk: the first failed write latches
// until the host sends a byte, so the rest of the GET fails at once.
void stalled_host_fails_get_fast() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  auto r = std::make_unique<Rig>(true, true, static_cast<std::uint16_t>(1U << 4));
  r->run(10000);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  g_host_stalled = true;
  g_serial_room = 0;
  const std::uint64_t before = g_us;
  g_serial_in = cdc_frame('G', "Scene01");
  r->run(5);
  CHECK(r->engine.maintenance_session().counters().output_failures >= 2);
  CHECK(g_us - before < 4000000U);          // one 2 s wait at most, not one per chunk
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  g_host_stalled = false;
  g_serial_room = 64;
  g_serial_in += 'Q';
  r->run(200);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(g_unguarded_writes == 0);
}

// The entry gesture is decided by the first pad sample only: a pad pressed after boot does
// not enter maintenance (boot.py selects USB mode only for keys held at power-up).
void pad_pressed_after_boot_does_not_enter_maintenance() {
  auto r = std::make_unique<Rig>();
  r->key(9, true);
  r->run(10000);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->count(MidiKind::NoteOn, 60) == 1);   // it is an ordinary note instead
  r->key(9, false);
  r->run(10);
}

void maintenance_rejects_frames_and_stops_on_idle_host() {
  auto r = std::make_unique<Rig>(true, false, static_cast<std::uint16_t>(1U << 4));  // pad 4 held at boot
  r->run(10000);                            // hold past 500 ms in the entry window
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  g_key_mask = 0;                           // physical release while quiesced
  // A PUT of a scene name outside the allowlist is refused with a NAK; nothing is written.
  g_serial_in = cdc_frame('P', "Scene17");
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'N');
  CHECK(r->engine.maintenance_session().counters().disallowed == 1);
  CHECK(t13::shared_fs().mutations == 0);
  // No byte for 60 s returns the device to performance on its own.
  for (int i = 0; i < 61; ++i) {
    g_us += 1000000;
    r->ui.poll();
    r->engine.poll();
  }
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(g_unguarded_writes == 0);
}

// The boot index is the pointer's scene even when that scene is missing (scenes.py keeps
// state.active_scene from read_active_scene). A save then targets that scene, never another.
void boot_pointer_keeps_index_when_scene_missing() {
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), scene_with_key(5).c_str());
  t13::shared_fs().put(pointer_id(), "4\n");
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  CHECK(r->ctl().active_scene() == 3);
  CHECK(r->ctl().last_scene_status() == SceneServiceStatus::SceneError);
  CHECK(r->ctl().global().key == 0);        // Scene04 missing: stock defaults in memory
  CHECK(t13::shared_fs().mutations == 0);
  CHECK(r->engine.controller().request_scene_save());
  CHECK(r->run_until_idle());
  CHECK(t13::shared_fs().cell(scene_id(3)).exists);
  CHECK(t13::shared_fs().equals(scene_id(0), scene_with_key(5).c_str()));  // Scene01 untouched
  CHECK(t13::shared_fs().equals(pointer_id(), "4\n"));
  CHECK(r->ctl().diagnostics().scene_failures == 0);  // a missing scene at boot is not a failure
}

// Source load order (scenes.load_scene): S records (globals, including key_offset) come before
// the M record that constructs chord_walk, whose __init__ sets key_offset to -2. The saved
// value is therefore overwritten, as in the source. Pins the order (apply_staging: globals
// first, then module construction).
void chord_walk_scene_sets_source_key_offset() {
  std::string text = scene_with_key(0);
  const std::size_t offset = text.find("S,key_offset,0\n");
  text.replace(offset + 13, 1, "3");
  const std::size_t module = text.find("M,1,note,1\n");
  text.replace(module + 4, 4, "chord_walk");   // "M,1," then the module name
  // The note module's P,key_offset (PADOFS) line would set the global after construction; a
  // chord_walk scene without that parameter line keeps the constructor's -2.
  const std::size_t parm = text.find("P,key_offset,0\n");
  text.erase(parm, std::strlen("P,key_offset,0\n"));
  t13::shared_fs().reset();
  put_names();
  t13::shared_fs().put(scene_id(0), text.c_str());
  auto r = std::make_unique<Rig>(false, true);
  r->settle();
  CHECK(r->ctl().last_scene_status() == SceneServiceStatus::Succeeded);
  CHECK(std::strcmp(r->ctl().slot_module_id(kSlotIn), "chord_walk") == 0);
  CHECK(r->ctl().global().key_offset == -2);
}

void registry_order_matches_source() {
  // registry._registry.MODULE_SPECS order with phraser hidden (no implementation).
  static const char* const kSource[] = {
      "empty", "arp", "arp_walk", "bounce", "chord", "chopper", "delay", "drop", "euclid",
      "gate", "latch", "pick", "quantize", "randomize", "retrigger", "strum", "transpose", "wander"};
  constexpr std::size_t kCount = sizeof kSource / sizeof kSource[0];
  // The source list is the core prefix; enabled user modules are appended after it.
  CHECK(module_count() == orbit::kGeneratedProcessorCount && kCount <= module_count());
  for (std::size_t i = 0; i < kCount; ++i) {
    const ModuleDescriptor* d = module_at(static_cast<std::uint8_t>(i));
    CHECK(d != nullptr && std::strcmp(d->id, kSource[i]) == 0);
    CHECK(find_module(kSource[i]) == d);
  }
  CHECK(find_module("phraser") == nullptr);
  // The user-visible selector is the same order (ui_model selector table).
  CHECK(processor_selector_count() == kCount);
  for (std::size_t i = 0; i < kCount; ++i)
    CHECK(std::strcmp(processor_selector(static_cast<std::uint8_t>(i)).id, kSource[i]) == 0);
}

// ---- Host-initiated maintenance (ENTER-MAINT / EXIT-MAINT / INFO in normal run) ----
// Start (encoder held + the second pad) and stop (encoder held + the first pad), as the device.
void start_clock_shortcut(Rig& r) {
  r.run_until_idle();
  r.encoder(true);
  r.run(3);
  r.key(1, true);
  r.run(3);
  r.key(1, false);
  r.run(3);
  r.encoder(false);
  r.run(3);
  r.run_until_idle();
}

// True when out holds a complete frame of the given type whose name is the given text.
bool reply_is(const std::string& out, char type, const std::string& name) {
  return out.size() >= 8 + name.size() && out[2] == type &&
         static_cast<unsigned char>(out[3]) == name.size() && out.compare(8, name.size(), name) == 0;
}

// ENTER is refused, with a reason and no change, while the engine is not safely stopped:
// transport running, a note held, or the name editor open. Nothing is sent to the sound output.
// ENTER is refused with NotStopped while the transport runs. The performance continues.
void enter_refused_while_transport_runs() {
  auto r = std::make_unique<Rig>();
  start_clock_shortcut(*r);
  CHECK(r->ctl().transport().running());
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(20);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::NotStopped));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->ctl().transport().running());
  CHECK(!r->ctl().maintenance_hold());
}

// ENTER is refused with NotStopped while a note is held on the input pad; the note keeps sounding
// and nothing is sent to the output by the refused request.
void enter_refused_while_note_held() {
  auto r = std::make_unique<Rig>();
  r->run(200);
  r->key(3, true);
  r->run(60);
  CHECK(r->ctl().output().held_count() >= 1);
  const std::size_t sounding = r->count(MidiKind::NoteOn);
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(20);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::NotStopped));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->count(MidiKind::NoteOn) == sounding);
  CHECK(r->ctl().output().held_count() >= 1);
}

// ENTER is refused with Busy while the name editor is open ([NEW] on the SCENE row).
void enter_refused_in_name_editor() {
  auto r = std::make_unique<Rig>();
  goto_slot(*r, kSlotSettings);
  click(*r);                                   // SCENE row: parameter selection
  click(*r);                                   // parameter edit
  turn(*r, 10);                                // ten named slots: index 10 is [NEW]
  click(*r);                                   // the name editor opens
  CHECK(r->ctl().section() == UiSection::NameEdit);
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(20);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::Busy));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->ctl().section() == UiSection::NameEdit);   // nothing changed in the editor
}

// A scene operation in flight refuses ENTER (Busy); the operation completes unchanged.
void enter_refused_while_scene_operation_runs() {
  auto r = std::make_unique<Rig>();
  CHECK(r->engine.controller().scene_service().request(SceneOperation::Save));
  CHECK(r->ctl().scene_busy());
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(2);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::Busy));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->run_until_idle());
  CHECK(g_unguarded_writes == 0);
}

// Refused frames in normal run change nothing: a wrong token, a bad CRC, and EXIT (not in
// maintenance). A partial ENTER completes across passes and is then accepted.
void normal_run_frame_rules() {
  auto r = std::make_unique<Rig>();
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAIN");
  r->run(10);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::BadToken));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);

  g_serial_out.clear();
  std::string bad = cdc_frame('M', "ENTER-MAINT");
  bad.back() = static_cast<char>(bad.back() ^ 0x01);
  g_serial_in = bad;
  r->run(10);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::BadChecksum));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);

  g_serial_out.clear();
  g_serial_in = cdc_frame('E', "EXIT-MAINT");
  r->run(10);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::NotInMaintenance));

  g_serial_out.clear();
  const std::string enter = cdc_frame('M', "ENTER-MAINT");
  g_serial_in = enter.substr(0, 5);
  r->run(20);
  CHECK(g_serial_out.empty());
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  g_serial_in = enter.substr(5);
  r->run(60);
  CHECK(reply_is(g_serial_out, 'A', "ENTER-MAINT"));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  g_serial_in += 'Q';
  r->run(200);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
}

// The normal run takes the INFO frame and the '?' dump in any order, around garbage; the dump
// text is unchanged and the INFO reply carries the device state.
void info_and_dump_in_normal_run() {
  auto r = std::make_unique<Rig>();
  g_serial_out.clear();
  g_serial_in = std::string("zz?") + cdc_frame('I', "") + std::string("\x4F\x00", 2) + "?";
  r->run(600);
  const std::size_t info_at = g_serial_out.find(std::string("\x4F\x31\x44\x00", 4));
  CHECK(info_at != std::string::npos);
  CHECK(g_serial_out.find("INFO fw=", info_at) != std::string::npos);
  CHECK(g_serial_out.find("MODULES count=24 core=note,chords", info_at) != std::string::npos);
  CHECK(g_serial_out.find("STORAGE mounted=1 scenes=10 names_ok=1", info_at) != std::string::npos);
  CHECK(g_serial_out.find("STATE maint=0 transport=0", info_at) != std::string::npos);
  CHECK(g_serial_out.find("ORBIT12-SLICE") != std::string::npos);     // the dump still answers '?'
  CHECK(g_serial_out.find("RUNCMD commands=") != std::string::npos);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(r->engine.controller().scene_service().failure_count() == 0);
}

// ENTER is accepted when stopped and ACKed only once Active; LIST and INFO work in maintenance;
// ENTER and a stray data request are refused there; EXIT returns to normal in place; scene
// editing and notes work afterwards; no note is left stuck.
void enter_and_exit_round_trip() {
  auto r = std::make_unique<Rig>();
  r->run(200);
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(50);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);
  CHECK(g_window_open);
  CHECK(reply_is(g_serial_out, 'A', "ENTER-MAINT"));
  CHECK(r->ctl().maintenance_hold());

  g_serial_out.clear();
  g_serial_in = cdc_frame('L', "");
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'D');

  g_serial_out.clear();
  g_serial_in = cdc_frame('G', "scene_names");      // GET in maintenance: the names file is served
  r->run(200);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'D');
  CHECK(r->engine.maintenance_session().counters().served == 2);

  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT");
  r->run(50);
  CHECK(nak_reason(g_serial_out) == static_cast<int>(maintenance::NakReason::AlreadyInMaintenance));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Active);

  g_serial_out.clear();
  g_serial_in = cdc_frame('I', "");
  r->run(50);
  CHECK(g_serial_out.size() >= 3 && g_serial_out[2] == 'D');
  CHECK(g_serial_out.find("STATE maint=1 transport=0") != std::string::npos);

  g_serial_out.clear();
  g_serial_in = cdc_frame('E', "EXIT-MAINT");
  r->run(100);
  CHECK(reply_is(g_serial_out, 'A', "EXIT-MAINT"));
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
  CHECK(!r->ctl().maintenance_hold());
  CHECK(!g_window_open);
  CHECK(g_unguarded_writes == 0);
  r->run(50);                                           // resync after the exit, as with 'Q'

  g_serial_out.clear();
  g_serial_in = cdc_frame('I', "");
  r->run(50);
  CHECK(g_serial_out.find("STATE maint=0 transport=0") != std::string::npos);

  const std::size_t before = r->count(MidiKind::NoteOn);
  r->key(3, true);
  r->run(5);
  CHECK(r->count(MidiKind::NoteOn) == before + 1);      // performance input works again in place
  r->key(3, false);
  r->run(20);
  CHECK(r->count(MidiKind::NoteOn) == r->count(MidiKind::NoteOff));   // no stuck note

  CHECK(r->engine.controller().scene_service().request(SceneOperation::Save));
  CHECK(r->run_until_idle());
  CHECK(r->engine.controller().scene_service().failure_count() == 0);
  CHECK(g_unguarded_writes == 0);
}

// A reply queued while a dump streams waits until the dump ends: INFO never lands inside the dump
// text, and the dump is complete before the frame starts.
void reply_never_splices_into_a_dump() {
  auto r = std::make_unique<Rig>();
  g_serial_out.clear();
  g_serial_in = "?";
  r->run(2);                                             // the dump is mid-stream (64 bytes a pass)
  CHECK(g_serial_out.find("END\n") == std::string::npos);
  g_serial_in = cdc_frame('I', "");
  r->run(800);
  const std::size_t end = g_serial_out.find("END\n");
  const std::size_t info = g_serial_out.find(std::string("\x4F\x31\x44\x00", 4));
  CHECK(end != std::string::npos && info != std::string::npos && info > end);
  CHECK(g_serial_out.find("INFO fw=", info) != std::string::npos);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
}

// A stray '?' (or a frame) inside a partial frame that stops: the partial frame is abandoned, its
// bytes are scanned again, and the '?' after the gap still dumps.
void partial_frame_gap_keeps_question_mark() {
  auto r = std::make_unique<Rig>();
  g_serial_out.clear();
  g_serial_in = cdc_frame('M', "ENTER-MAINT").substr(0, 6);
  r->run(20);
  CHECK(g_serial_out.empty());
  r->run(3000);                                          // more than the 250 ms gap
  g_serial_out.clear();
  g_serial_in = "?";
  r->run(600);
  CHECK(g_serial_out.find("ORBIT12-SLICE") != std::string::npos);
  CHECK(r->engine.maintenance_state() == MaintenanceState::Off);
}

}  // namespace

int main() {
  latency_stats();
  text_builders();
  dump_close_reports_truncation();
  registry_order_matches_source();
  key_to_midi_and_clock_accounting();
  snapshots_feed_pixels_and_screen();
  sleep_and_wake();
  serial_dump_is_framed_and_read_only();
  bridge_events_ordered_with_encoder();
  first_boot_creates_alfa_once();
  boot_with_scene_loads_active_scene();
  corrupt_scene_falls_back_to_defaults();
  scene_switch_is_in_place();
  empty_fs_gesture_save_switch_round_trip();
  corrupt_target_switch_is_refused_and_row_shows_active();
  failed_switch_then_gesture_recovers_in_place();
  failed_recovery_counts_each_failure();
  maintenance_entry_and_exit_release_notes();
  maintenance_rejects_frames_and_stops_on_idle_host();
  enter_refused_while_transport_runs();
  enter_refused_while_note_held();
  enter_refused_in_name_editor();
  enter_refused_while_scene_operation_runs();
  normal_run_frame_rules();
  info_and_dump_in_normal_run();
  enter_and_exit_round_trip();
  partial_frame_gap_keeps_question_mark();
  reply_never_splices_into_a_dump();
  unmounted_boot_maintenance_format_recovers();
  stalled_host_fails_get_fast();
  pad_pressed_after_boot_does_not_enter_maintenance();
  boot_pointer_keeps_index_when_scene_missing();
  chord_walk_scene_sets_source_key_offset();
  if (g_failures == 0) std::printf("t14_slice: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
