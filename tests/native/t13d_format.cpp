// T13 explicit format: the verified format sequence (fsformat) against a fake LittleFS with
// power-cut injection, and the maintenance FORMAT frame (token, gate, failures, no accidental
// trigger). The LittleFS adapter itself is Arduino-only and is compiled by the target build.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "persistence/fs_format.hpp"
#include "persistence/maintenance_protocol.hpp"
#include "persistence/scene_codec.hpp"
#include "t13_fs.hpp"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++failures; } } while (false)

namespace of = orbit::fsformat;
namespace mt = orbit::maintenance;
using orbit::maintenance::NakReason;

// ---- Fake LittleFS partition ----------------------------------------------------------
// Mutations are format, each file write, and each remove. cut_after = k lets k mutations
// complete; the next one loses power (a torn write keeps the first half of its bytes).
// Power loss clears the mount state; power_cycle() models the next boot.
struct Flash {
  static constexpr std::size_t kFiles = 4;
  static constexpr std::size_t kBytes = 96;
  const char* names[kFiles] = {of::kStampName, of::kProbeName, "/Scene01", "/user_settings"};
  struct File {
    char bytes[kBytes]{};
    std::size_t size = 0;
    bool exists = false;
  };
  File files[kFiles]{};
  bool formatted = false;      // a completed format (superblock) is present
  bool mounted = false;
  bool powered_off = false;
  int cut_after = -1;
  int mutations = 0;
  bool fail_format = false;
  bool fail_mount = false;
  bool drop_writes = false;    // report success but store nothing (silent short write)

  int index_of(const char* name) const noexcept {
    for (std::size_t i = 0; i < kFiles; ++i)
      if (std::strcmp(names[i], name) == 0) return static_cast<int>(i);
    return -1;
  }
  bool mutate() noexcept {
    if (powered_off) return false;
    if (cut_after == 0) { powered_off = true; return false; }
    if (cut_after > 0) --cut_after;
    ++mutations;
    return true;
  }
  void reset() noexcept {
    for (auto& file : files) file = File{};
    formatted = false;
    mounted = false;
    powered_off = false;
    cut_after = -1;
    mutations = 0;
    fail_format = fail_mount = drop_writes = false;
  }
  void power_cycle() noexcept {
    powered_off = false;
    mounted = false;
    cut_after = -1;
  }
  void put(const char* name, const char* text) noexcept {
    auto& file = files[index_of(name)];
    const std::size_t length = std::strlen(text);
    std::memcpy(file.bytes, text, length);
    file.size = length;
    file.exists = true;
  }
  bool equals(const char* name, const char* text) const noexcept {
    const auto& file = files[index_of(name)];
    return file.exists && file.size == std::strlen(text) &&
           std::memcmp(file.bytes, text, file.size) == 0;
  }
};

bool format_op(void* context) {
  auto& flash = *static_cast<Flash*>(context);
  flash.formatted = false;  // the superblock is rewritten: until the format completes, no mount
  if (flash.fail_format) return false;
  if (!flash.mutate()) return false;
  for (auto& file : flash.files) file = Flash::File{};
  flash.formatted = true;
  return true;
}

bool mount_op(void* context) {
  auto& flash = *static_cast<Flash*>(context);
  if (flash.powered_off || flash.fail_mount || !flash.formatted) return false;
  flash.mounted = true;
  return true;
}

void unmount_op(void* context) { static_cast<Flash*>(context)->mounted = false; }

bool write_op(void* context, const char* name, const char* text, std::size_t length) {
  auto& flash = *static_cast<Flash*>(context);
  const int index = flash.index_of(name);
  if (!flash.mounted || index < 0 || length > Flash::kBytes) return false;
  auto& file = flash.files[index];
  if (!flash.mutate()) {
    // Torn write: the first half of the bytes reached the flash.
    const std::size_t half = length / 2;
    std::memcpy(file.bytes, text, half);
    file.size = half;
    file.exists = true;
    return false;
  }
  if (flash.drop_writes) {
    file.size = 0;
    file.exists = true;
    return true;
  }
  std::memcpy(file.bytes, text, length);
  file.size = length;
  file.exists = true;
  return true;
}

orbit::fsformat::FileRead read_op(void* context, const char* name, char* out, std::size_t capacity,
                                  std::size_t& length) {
  auto& flash = *static_cast<Flash*>(context);
  length = 0;
  const int index = flash.index_of(name);
  if (!flash.mounted || index < 0) return of::FileRead::Error;
  const auto& file = flash.files[index];
  if (!file.exists) return of::FileRead::Missing;
  if (file.size > capacity) return of::FileRead::Error;
  std::memcpy(out, file.bytes, file.size);
  length = file.size;
  return of::FileRead::Ok;
}

bool remove_op(void* context, const char* name) {
  auto& flash = *static_cast<Flash*>(context);
  const int index = flash.index_of(name);
  if (!flash.mounted || index < 0) return false;
  auto& file = flash.files[index];
  if (!file.exists) return true;
  if (!flash.mutate()) return false;
  file.exists = false;
  file.size = 0;
  return true;
}

of::FsBackend backend(Flash& flash) noexcept {
  of::FsBackend b;
  b.context = &flash;
  b.format = format_op;
  b.mount = mount_op;
  b.unmount = unmount_op;
  b.write_file = write_op;
  b.read_file = read_op;
  b.remove_file = remove_op;
  return b;
}

Flash& shared_flash() noexcept {
  static Flash flash;
  return flash;
}

// ---- Format sequence tests ------------------------------------------------------------

void test_success_erases_verifies_and_mounts() {
  auto& flash = shared_flash();
  flash.reset();
  flash.put("/Scene01", "old scene data");  // leftover data from an earlier partition use
  flash.put("/user_settings", "key_brightness,0.5\n");
  const auto b = backend(flash);
  CHECK(of::format_verified(b) == of::FormatResult::Ok);
  CHECK(flash.mounted);
  CHECK(flash.mutations == 4);                       // format, probe write, probe remove, stamp
  CHECK(!flash.files[flash.index_of(of::kProbeName)].exists);  // probe removed
  CHECK(flash.equals(of::kStampName, of::kStampText));
  CHECK(!flash.files[2].exists);                    // old data erased
  CHECK(!flash.files[3].exists);
  // The filesystem is usable after the format: a scene-sized write reads back.
  CHECK(write_op(&flash, "/Scene01", "SCN1 test", 9));
  char buffer[of::kTextCapacity]{};
  std::size_t got = 0;
  CHECK(read_op(&flash, "/Scene01", buffer, sizeof(buffer), got) == of::FileRead::Ok);
  CHECK(got == 9 && std::memcmp(buffer, "SCN1 test", 9) == 0);
  // The next boot mounts it (stamp present).
  flash.mounted = false;
  CHECK(of::mount_verified(b));
  CHECK(flash.mounted);
}

void test_failures_leave_partition_unmounted() {
  auto& flash = shared_flash();
  const auto b = backend(flash);

  flash.reset();
  flash.fail_format = true;
  CHECK(of::format_verified(b) == of::FormatResult::FormatFailed);
  CHECK(!flash.mounted);

  flash.reset();
  flash.fail_mount = true;
  CHECK(of::format_verified(b) == of::FormatResult::MountFailed);
  CHECK(!flash.mounted);

  flash.reset();
  flash.drop_writes = true;  // probe reads back empty
  CHECK(of::format_verified(b) == of::FormatResult::VerifyFailed);
  CHECK(!flash.mounted);
}

void test_boot_mount_requires_stamp() {
  auto& flash = shared_flash();
  const auto b = backend(flash);

  // Never formatted (CIRCUITPY/FAT or blank): mount fails, nothing is mounted or erased.
  flash.reset();
  flash.put("/Scene01", "kept");
  CHECK(!of::mount_verified(b));
  CHECK(!flash.mounted);
  CHECK(flash.equals("/Scene01", "kept"));

  // LittleFS-formatted but without the stamp (an interrupted format): reported unmounted.
  flash.reset();
  flash.formatted = true;
  CHECK(!of::mount_verified(b));
  CHECK(!flash.mounted);

  // A stamp with other text is not accepted.
  flash.reset();
  flash.formatted = true;
  flash.put(of::kStampName, "ORBIT12");
  CHECK(!of::mount_verified(b));
  CHECK(!flash.mounted);
}

// Power cut after each mutation of a format: every cut before the last mutation (the stamp
// write) must leave the next boot unmounted; a retry then succeeds. A cut inside the stamp
// write leaves a torn stamp and is also unmounted.
void test_power_cut_sweep_reports_unmounted() {
  auto& flash = shared_flash();
  const auto b = backend(flash);

  // Mutation count of an uninterrupted format (see test_success_erases_verifies_and_mounts).
  constexpr int kMutations = 4;
  for (int cut = 0; cut < kMutations; ++cut) {
    for (int start = 0; start < 2; ++start) {  // blank partition, then a formatted one
      flash.reset();
      flash.formatted = start == 1;
      flash.put("/Scene01", "old");
      flash.cut_after = cut;
      const of::FormatResult result = of::format_verified(b);
      CHECK(result != of::FormatResult::Ok);
      CHECK(!flash.mounted);
      flash.power_cycle();
      CHECK(!of::mount_verified(b));  // next boot: unmounted, not corrupt-mounted
      CHECK(!flash.mounted);
      // A retry from that state completes and verifies.
      CHECK(of::format_verified(b) == of::FormatResult::Ok);
      CHECK(flash.mounted);
      flash.mounted = false;
      CHECK(of::mount_verified(b));
    }
  }
  // No cut: the same sequence completes.
  flash.reset();
  flash.cut_after = -1;
  CHECK(of::format_verified(b) == of::FormatResult::Ok);
}

// ---- Maintenance FORMAT frame ---------------------------------------------------------

struct Capture {
  std::uint8_t bytes[1 << 12]{};
  std::size_t size = 0;
  void clear() noexcept { size = 0; }
};
bool capture_write(void* context, const std::uint8_t* data, std::size_t length) {
  auto& out = *static_cast<Capture*>(context);
  if (out.size + length > sizeof(out.bytes)) return false;
  std::memcpy(out.bytes + out.size, data, length);
  out.size += length;
  return true;
}
struct Gate { bool stopped = true; };
bool gate_stopped(void* context) { return static_cast<Gate*>(context)->stopped; }

bool schema_lookup(void*, std::uint8_t slot, const char* id, orbit::SceneModuleSchema& result) {
  const auto* schema = orbit::fixed_scene_schema(slot, id);
  if (schema == nullptr) return false;
  result = *schema;
  return true;
}
bool stub_true(void*) { return true; }
void stub_nop(void*) {}
std::uint32_t stub_quiesce(void*, std::uint32_t, std::uint32_t) { return 1; }
bool stub_quiesced(void*, std::uint32_t) { return true; }
bool stub_timed_out(void*, std::uint32_t, std::uint32_t) { return false; }
void stub_cancel(void*, std::uint32_t) {}
void stub_resume(void*, std::uint32_t) {}
bool stub_capture(void*, orbit::SceneDto&) { return true; }
bool stub_globals(void*, const orbit::GlobalState&, std::uint8_t) { return true; }
bool stub_construct(void*, std::uint8_t, const char*, std::uint16_t, bool) { return true; }
bool stub_parameter(void*, std::uint8_t, const orbit::SceneParameter&) { return true; }
bool stub_attribute(void*, std::uint8_t, const orbit::SceneAttribute&) { return true; }
orbit::SceneLifecyclePort stub_lifecycle() {
  orbit::SceneLifecyclePort port{};
  port.is_stopped = stub_true;
  port.stop_release_drain = stub_nop;
  port.request_quiesce = stub_quiesce;
  port.quiesced = stub_quiesced;
  port.quiesce_timed_out = stub_timed_out;
  port.cancel_quiesce = stub_cancel;
  port.resume = stub_resume;
  port.capture = stub_capture;
  port.reset_defaults = stub_true;
  port.prepare_scene = stub_true;
  port.apply_globals = stub_globals;
  port.construct_module = stub_construct;
  port.apply_parameter = stub_parameter;
  port.apply_attribute = stub_attribute;
  port.finish_scene = stub_true;
  port.rebuild_ui = stub_nop;
  return port;
}

// Format callback under test: the real sequence against the shared fake partition.
int g_format_calls = 0;
of::FormatResult format_callback(void*) {
  ++g_format_calls;
  return of::format_verified(backend(shared_flash()));
}

std::size_t build_frame(char type, const char* name, std::size_t name_length,
                        const std::uint8_t* payload, std::uint32_t payload_length,
                        std::uint8_t* out) {
  out[0] = 0x4F; out[1] = 0x31; out[2] = static_cast<std::uint8_t>(type);
  out[3] = static_cast<std::uint8_t>(name_length);
  out[4] = static_cast<std::uint8_t>(payload_length); out[5] = static_cast<std::uint8_t>(payload_length >> 8);
  out[6] = static_cast<std::uint8_t>(payload_length >> 16); out[7] = static_cast<std::uint8_t>(payload_length >> 24);
  std::size_t n = 8;
  std::memcpy(out + n, name, name_length); n += name_length;
  if (payload_length != 0) { std::memcpy(out + n, payload, payload_length); n += payload_length; }
  std::uint32_t crc = mt::crc32_init();
  for (std::size_t i = 0; i < n; ++i) crc = mt::crc32_update(crc, out[i]);
  crc = mt::crc32_final(crc);
  for (int i = 0; i < 4; ++i) out[n++] = static_cast<std::uint8_t>(crc >> (8 * i));
  return n;
}

// Type and reason of the first response frame; crc_ok reports its checksum.
char first_type(const Capture& out, std::uint8_t& reason, bool& crc_ok) {
  crc_ok = false;
  reason = 0;
  if (out.size < 12 || out.bytes[0] != 0x4F || out.bytes[1] != 0x31) return 0;
  const std::size_t name_length = out.bytes[3];
  const std::uint32_t payload = out.bytes[4] | (out.bytes[5] << 8) | (out.bytes[6] << 16) |
                                (static_cast<std::uint32_t>(out.bytes[7]) << 24);
  const std::size_t body = 8 + name_length + payload;
  if (out.size < body + 4) return 0;
  std::uint32_t crc = mt::crc32_init();
  for (std::size_t i = 0; i < body; ++i) crc = mt::crc32_update(crc, out.bytes[i]);
  const std::uint32_t got = out.bytes[body] | (out.bytes[body + 1] << 8) |
                            (out.bytes[body + 2] << 16) |
                            (static_cast<std::uint32_t>(out.bytes[body + 3]) << 24);
  crc_ok = mt::crc32_final(crc) == got;
  if (payload == 1) reason = out.bytes[8 + name_length];
  return static_cast<char>(out.bytes[2]);
}

struct Harness {
  t13::Fs& fs = t13::shared_fs();
  orbit::SceneSchemaResolver schema{nullptr, schema_lookup};
  orbit::SceneService service{t13::port(fs), stub_lifecycle(), schema};
  Capture out{};
  Gate gate{};
  mt::MaintenanceSession session;

  explicit Harness(bool with_format_port = true)
      : session(t13::port(fs), schema, service, {&out, capture_write}, {&gate, gate_stopped}) {
    fs.reset();
    g_format_calls = 0;
    if (with_format_port) session.set_format({nullptr, format_callback});
  }
  void feed(const std::uint8_t* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) session.feed(data[i]);
  }
};

// Sends one frame; returns the response type and reason.
char exchange(Harness& h, const std::uint8_t* frame, std::size_t length, std::uint8_t& reason) {
  h.out.clear();
  h.feed(frame, length);
  bool crc_ok = false;
  const char type = first_type(h.out, reason, crc_ok);
  CHECK(h.out.size == 0 || crc_ok);
  return type;
}

void test_format_frame_requires_token_and_gate() {
  std::uint8_t frame[64];
  std::uint8_t reason = 0;

  // Correct token, maintenance stopped, partition blank: ACK after a verified format.
  {
    Harness h;
    shared_flash().reset();
    const std::size_t n = build_frame('F', orbit::maintenance::kFormatToken,
                                      orbit::maintenance::kFormatTokenLength, nullptr, 0, frame);
    CHECK(exchange(h, frame, n, reason) == 'A');
    CHECK(g_format_calls == 1);
    CHECK(h.session.counters().formats == 1);
    CHECK(shared_flash().mounted);
    CHECK(shared_flash().equals(of::kStampName, of::kStampText));
  }
  // Wrong token (one character changed), lowercase, missing name: each rejected, no format.
  const char* wrong[] = {"ERASE-FX", "erase-fs", "ERASE-F", "ERASE-FS2", "/ERASE-F"};
  for (const char* token : wrong) {
    Harness h;
    const std::size_t length = std::strlen(token);
    const std::size_t n = build_frame('F', token, length, nullptr, 0, frame);
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::BadToken));
    CHECK(g_format_calls == 0);
  }
  {
    Harness h;
    const std::size_t n = build_frame('F', "", 0, nullptr, 0, frame);
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::BadToken));
    CHECK(g_format_calls == 0);
  }
  // Correct token with a payload: refused as a bad frame.
  {
    Harness h;
    const std::uint8_t payload[1] = {0x00};
    const std::size_t n = build_frame('F', orbit::maintenance::kFormatToken,
                                      orbit::maintenance::kFormatTokenLength, payload, 1, frame);
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::BadFrame));
    CHECK(g_format_calls == 0);
  }
  // Correct token outside maintenance (engine not stopped): Busy, nothing erased.
  {
    Harness h;
    h.gate.stopped = false;
    const std::size_t n = build_frame('F', orbit::maintenance::kFormatToken,
                                      orbit::maintenance::kFormatTokenLength, nullptr, 0, frame);
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::Busy));
    CHECK(g_format_calls == 0);
  }
  // Correct token with a bad checksum: rejected, nothing erased.
  {
    Harness h;
    const std::size_t n = build_frame('F', orbit::maintenance::kFormatToken,
                                      orbit::maintenance::kFormatTokenLength, nullptr, 0, frame);
    frame[n - 1] ^= 0x5A;
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::BadChecksum));
    CHECK(g_format_calls == 0);
  }
}

void test_format_failures_nak_with_reason() {
  std::uint8_t frame[64];
  std::uint8_t reason = 0;
  const std::size_t n = build_frame('F', orbit::maintenance::kFormatToken,
                                    orbit::maintenance::kFormatTokenLength, nullptr, 0, frame);
  {
    Harness h;
    shared_flash().reset();
    shared_flash().fail_format = true;
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::FormatFailed));
    CHECK(h.session.counters().format_failures == 1);
    CHECK(h.session.counters().formats == 0);
    CHECK(!shared_flash().mounted);
  }
  {
    Harness h;
    shared_flash().reset();
    shared_flash().fail_mount = true;
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::MountFailed));
    CHECK(!shared_flash().mounted);
  }
  {
    Harness h;
    shared_flash().reset();
    shared_flash().drop_writes = true;
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::VerifyFailed));
    CHECK(!shared_flash().mounted);
  }
  {
    Harness h(false);  // no callback installed: refused, never acknowledged
    shared_flash().reset();
    CHECK(exchange(h, frame, n, reason) == 'N');
    CHECK(reason == static_cast<std::uint8_t>(NakReason::FormatFailed));
    CHECK(!shared_flash().mounted);
  }
  shared_flash().reset();
}

// Existing frame types, including a PUT and GET aimed at the token name, never format.
void test_existing_frames_never_format() {
  Harness h;
  shared_flash().reset();
  std::uint8_t frame[128];
  std::uint8_t reason = 0;
  std::uint8_t payload[8] = {'x', 'y', 'z', '\n', 0, 0, 0, 0};
  const std::size_t list = build_frame('L', "", 0, nullptr, 0, frame);
  CHECK(exchange(h, frame, list, reason) == 'D');
  const std::size_t get = build_frame('G', "Scene01", 7, nullptr, 0, frame);
  CHECK(exchange(h, frame, get, reason) == 'N');  // Scene01 absent after reset: NotFound
  CHECK(reason == static_cast<std::uint8_t>(NakReason::NotFound));
  const std::size_t put = build_frame('P', orbit::maintenance::kFormatToken,
                                      orbit::maintenance::kFormatTokenLength, payload, 4, frame);
  CHECK(exchange(h, frame, put, reason) == 'N');
  CHECK(reason == static_cast<std::uint8_t>(NakReason::DisallowedName));
  const std::size_t get_token = build_frame('G', orbit::maintenance::kFormatToken,
                                            orbit::maintenance::kFormatTokenLength, nullptr, 0, frame);
  CHECK(exchange(h, frame, get_token, reason) == 'N');
  CHECK(reason == static_cast<std::uint8_t>(NakReason::DisallowedName));
  const std::size_t abort_frame = build_frame('X', "", 0, nullptr, 0, frame);
  CHECK(exchange(h, frame, abort_frame, reason) == 'A');
  CHECK(g_format_calls == 0);
  CHECK(!shared_flash().mounted);
  CHECK(!shared_flash().formatted);
}

}  // namespace

int main() {
  test_success_erases_verifies_and_mounts();
  test_failures_leave_partition_unmounted();
  test_boot_mount_requires_stamp();
  test_power_cut_sweep_reports_unmounted();
  test_format_frame_requires_token_and_gate();
  test_format_failures_nak_with_reason();
  test_existing_frames_never_format();
  if (failures != 0) {
    std::fprintf(stderr, "t13d_format: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("t13d_format: all checks passed");
  return 0;
}
