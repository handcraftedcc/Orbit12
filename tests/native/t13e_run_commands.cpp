// Normal-run maintenance commands (T13 maintenance entry): the byte framer that runs while the
// engine plays, the reply encoder and the INFO text. Behavior and bounds, not implementation:
// token exactness, CRC, partial and interleaved frames, garbage resynchronization, the '?' dump
// byte outside frames, bounded discard, gap replay and wrap-safe time. Golden frames were
// generated with Python zlib (scene_transfer.py selftest prints the same bytes).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "persistence/maintenance_command.hpp"
#include "persistence/maintenance_protocol.hpp"
#include "app/maintenance_info.hpp"
#include "app/module_descriptors.hpp"

namespace {
int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

using orbit::maintenance::NakReason;
using orbit::maintenance::RunCommand;
using orbit::maintenance::RunCommandFramer;
using orbit::maintenance::RunSink;

// Golden frames (zlib CRC-32, little-endian trailer).
const char kInfoFrame[] = "4F314900000000006AFADCB3";
const char kEnterFrame[] = "4F314D0B00000000454E5445522D4D41494E54A95EA0C5";
const char kExitFrame[] = "4F31450A00000000455849542D4D41494E54F2E28AB3";

std::string from_hex(const char* hex) {
  std::string out;
  for (std::size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2) {
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return 0;
    };
    out += static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
  }
  return out;
}

// Builds a frame with a valid CRC for any type, name and payload (for negative cases).
std::string make_frame(char type, const std::string& name, const std::string& payload) {
  std::string frame = {'\x4F', '\x31', type, static_cast<char>(name.size()),
                       static_cast<char>(payload.size() & 0xFFU), static_cast<char>((payload.size() >> 8) & 0xFFU),
                       static_cast<char>((payload.size() >> 16) & 0xFFU), static_cast<char>((payload.size() >> 24) & 0xFFU)};
  frame += name;
  frame += payload;
  std::uint32_t crc = orbit::maintenance::crc32_init();
  for (char ch : frame) crc = orbit::maintenance::crc32_update(crc, static_cast<std::uint8_t>(ch));
  crc = orbit::maintenance::crc32_final(crc);
  for (int shift = 0; shift < 32; shift += 8) frame += static_cast<char>((crc >> shift) & 0xFFU);
  return frame;
}

struct Collector {
  std::vector<RunCommand> commands;
  std::string passthrough;
};

void on_command(void* context, const RunCommand& command) noexcept {
  static_cast<Collector*>(context)->commands.push_back(command);
}
void on_passthrough(void* context, std::uint8_t byte) noexcept {
  static_cast<Collector*>(context)->passthrough += static_cast<char>(byte);
}

struct Harness {
  RunCommandFramer framer;
  Collector out;
  std::uint32_t now = 1000;

  RunSink sink() { return RunSink{&out, &on_command, &on_passthrough}; }
  void feed(const std::string& bytes) {
    for (char c : bytes) framer.feed(static_cast<std::uint8_t>(c), now, sink());
  }
  void poll() { framer.poll(now, sink()); }
  void wait(std::uint32_t ms) { now += ms; poll(); }
  void clear() { out.commands.clear(); out.passthrough.clear(); }
};

bool name_equals(const RunCommand& c, const char* text) {
  const std::size_t n = std::strlen(text);
  return c.name_length == n && std::memcmp(c.name, text, n) == 0;
}

void golden_frames_are_delivered_as_valid_commands() {
  Harness h;
  h.feed(from_hex(kInfoFrame));
  h.feed(from_hex(kEnterFrame));
  h.feed(from_hex(kExitFrame));
  CHECK(h.out.commands.size() == 3);
  CHECK(h.out.commands.size() == 3 && h.out.commands[0].verdict == NakReason::None);
  CHECK(h.out.commands.size() == 3 && h.out.commands[0].type == 'I' && h.out.commands[0].name_length == 0);
  CHECK(h.out.commands.size() == 3 && h.out.commands[1].verdict == NakReason::None);
  CHECK(h.out.commands.size() == 3 && h.out.commands[1].type == 'M' && name_equals(h.out.commands[1], "ENTER-MAINT"));
  CHECK(h.out.commands.size() == 3 && h.out.commands[2].verdict == NakReason::None);
  CHECK(h.out.commands.size() == 3 && h.out.commands[2].type == 'E' && name_equals(h.out.commands[2], "EXIT-MAINT"));
  CHECK(h.out.passthrough.empty());
  CHECK(h.framer.counters().commands == 3 && h.framer.counters().refused == 0);
  CHECK(!h.framer.mid_frame());
}

// The token must match exactly: a near miss with a valid CRC is refused with BadToken and is
// never delivered as a command the application would act on.
void token_must_match_exactly() {
  const char* near_misses[][2] = {
      {"M", "ENTER-MAIN"}, {"M", "enter-maint"}, {"M", "ENTER-MAINTX"}, {"M", "ENTER-MAINT "},
      {"M", "EXIT-MAINT"}, {"E", "ENTER-MAINT"}, {"E", "EXIT-MAIN"},  {"E", "exit-maint"},
  };
  for (const auto& miss : near_misses) {
    Harness h;
    h.feed(make_frame(miss[0][0], miss[1], ""));
    CHECK(h.out.commands.size() == 1);
    CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadToken);
  }
  Harness h;
  h.feed(make_frame('M', "", ""));   // empty name
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadToken);
  CHECK(h.framer.counters().bad_token == 1);
}

void bad_checksum_is_never_a_command() {
  std::string frame = from_hex(kEnterFrame);
  frame.back() = static_cast<char>(frame.back() ^ 0x01);   // flip a CRC bit
  Harness h;
  h.feed(frame);
  CHECK(h.out.commands.size() == 1);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadChecksum);
  CHECK(h.framer.counters().bad_checksum == 1);
  // A flipped name byte also fails the CRC, before any token check.
  std::string tampered = from_hex(kEnterFrame);
  tampered[8] = 'X';
  h.clear();
  h.feed(tampered);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadChecksum);
}

void partial_frame_completes_across_passes() {
  const std::string frame = from_hex(kEnterFrame);
  Harness h;
  h.feed(frame.substr(0, 3));
  h.wait(10);
  h.feed(frame.substr(3, 9));
  h.wait(10);
  h.feed(frame.substr(12));
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::None);
  CHECK(h.out.passthrough.empty());
  CHECK(h.framer.counters().gap_resyncs == 0);
}

// Stray bytes (including a lone 0x4F) between frames pass through or are skipped; frames around
// them are still parsed. The sync pattern 4F 4F 31 starts at the second 4F.
void garbage_between_frames_resynchronizes() {
  Harness h;
  const std::string garbage = std::string("zz") + '\x4F' + '\x00' + "q";
  h.feed(garbage);
  h.feed(from_hex(kInfoFrame));
  h.feed(std::string("\xFF") + '\x4F');
  h.feed(from_hex(kExitFrame));
  CHECK(h.out.commands.size() == 2);
  CHECK(h.out.commands.size() == 2 && h.out.commands[0].type == 'I');
  CHECK(h.out.commands.size() == 2 && h.out.commands[1].type == 'E');
  CHECK(h.out.passthrough == garbage + std::string("\xFF") + '\x4F');
}

// The '?' dump request is a byte outside frames: it reaches the dump before and after frames.
void question_mark_dump_is_unaffected_by_frames() {
  Harness h;
  h.feed("?");
  h.feed(from_hex(kInfoFrame));
  h.feed("?");
  h.feed(from_hex(kEnterFrame));
  h.feed("?");
  CHECK(h.out.passthrough == "???");
  CHECK(h.out.commands.size() == 2);
}

// A partial frame that stops arriving is abandoned after the gap, and its bytes are scanned
// again: a '?' that follows is not lost, and the abandoned frame produces no command.
void abandoned_partial_frame_replays_and_keeps_question_mark() {
  Harness h;
  const std::string partial = from_hex(kEnterFrame).substr(0, 9);   // 4F 31 4D 0B 00 00 00 00 45
  h.feed(partial);
  CHECK(h.out.commands.empty());
  h.wait(RunCommandFramer::kGapMs + 50);
  CHECK(h.framer.counters().gap_resyncs == 1);
  CHECK(h.out.passthrough == partial.substr(2));    // the bytes after the sync, scanned again
  h.feed("?");
  CHECK(h.out.passthrough == partial.substr(2) + "?");
  CHECK(h.out.commands.empty());
}

// The rest of an abandoned frame is not a frame: its tail is scanned as plain bytes.
void abandoned_frame_tail_is_not_a_command() {
  Harness h;
  const std::string frame = from_hex(kEnterFrame);
  h.feed(frame.substr(0, 10));
  h.wait(RunCommandFramer::kGapMs + 1);
  h.feed(frame.substr(10));
  CHECK(h.out.commands.empty());
}

// A data request (LIST, GET, PUT, ...) is refused without buffering: its payload, even when it
// contains '?', is discarded and never reaches the dump or the application.
void data_requests_are_refused_and_their_payload_discarded() {
  Harness h;
  const std::string put = make_frame('P', "Scene01", "?ab" + std::string(40, '?') + "\x4F\x31");
  h.feed(put);
  CHECK(h.out.commands.size() == 1);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::NotInMaintenance);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].name_length == 0);
  CHECK(h.out.passthrough.empty());
  CHECK(h.framer.counters().refused == 1);
  h.feed(from_hex(kInfoFrame));
  CHECK(h.out.commands.size() == 2 && h.out.commands[1].verdict == NakReason::None);
}

// INFO and ENTER carry no payload: a payload is refused with BadFrame, its bytes discarded.
void payload_on_fixed_frames_is_refused() {
  Harness h;
  h.feed(make_frame('I', "", "x?"));
  h.feed(make_frame('M', "ENTER-MAINT", std::string(3, 'z')));
  CHECK(h.out.commands.size() == 2);
  CHECK(h.out.commands.size() == 2 && h.out.commands[0].verdict == NakReason::BadFrame);
  CHECK(h.out.commands.size() == 2 && h.out.commands[1].verdict == NakReason::BadFrame);
  CHECK(h.out.passthrough.empty());
  CHECK(h.framer.counters().bad_frames == 2);
}

// RENAME is a data request: normal run refuses it (NotInMaintenance) and discards its bytes.
void rename_is_refused_in_normal_run() {
  Harness h;
  h.feed(make_frame('R', "01,ZULU", ""));
  CHECK(h.out.commands.size() == 1);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::NotInMaintenance);
  CHECK(h.out.passthrough.empty());
  h.feed(from_hex(kInfoFrame));
  CHECK(h.out.commands.size() == 2 && h.out.commands[1].verdict == NakReason::None);
}

void unknown_type_and_long_name_are_framing_errors() {
  Harness h;
  // Unknown type 'Z' with a declared payload: BadFrame at once, scanning resumes after the header.
  const std::string header_only = std::string("\x4F\x31Z\x00\x05\x00\x00\x00", 8);
  h.feed(header_only);
  h.feed("?");
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadFrame);
  CHECK(h.out.passthrough == "?");
  h.clear();
  // Name length above kNameMax (16): BadFrame.
  const std::string long_name = std::string("\x4F\x31M\x11\x00\x00\x00\x00", 8);
  h.feed(long_name);
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::BadFrame);
}

// A declared payload above the discard bound is not swallowed: the framer scans it instead, so a
// '?' in it is still answered (a bounded buffer cannot hold it, and the bytes are not a command).
void oversize_declared_payload_is_scanned_not_discarded() {
  Harness h;
  h.feed(std::string("\x4F\x31P\x07\xFF\xFF\xFF\xFF", 8) + "Scene01");
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::NotInMaintenance);
  h.feed("?");
  CHECK(h.out.passthrough == "Scene01?");   // the name bytes were scanned as plain bytes
}

// A discard that stops arriving is dropped after the gap without replay (payload is data).
void discard_gap_drops_without_replay() {
  Harness h;
  h.feed(make_frame('G', "Scene01", std::string(200, 'A')).substr(0, 8 + 7 + 50));
  h.wait(RunCommandFramer::kGapMs + 1);
  CHECK(h.framer.counters().discard_drops == 1);
  CHECK(h.out.passthrough.empty());
  h.feed("?");
  CHECK(h.out.passthrough == "?");
}

// Time is wrap-safe: a frame that straddles the 32-bit millisecond wrap is not abandoned early.
void gap_timeout_is_wrap_safe() {
  Harness h;
  h.now = 0xFFFFFF00U;
  h.feed(from_hex(kEnterFrame).substr(0, 4));
  h.now = 0xFFFFFFF0U;
  h.poll();
  CHECK(h.framer.counters().gap_resyncs == 0);   // 240 ms: still within the gap
  h.feed(from_hex(kEnterFrame).substr(4));
  CHECK(h.out.commands.size() == 1 && h.out.commands[0].verdict == NakReason::None);
  // A gap that crosses the wrap is measured across it: last byte at 0xFFFFFFF0, poll at 0x100.
  h.clear();
  h.now = 0xFFFFFFF0U;
  h.feed(from_hex(kEnterFrame).substr(0, 4));
  h.now = 0x00000100U;   // 272 ms after the last byte
  h.poll();
  CHECK(h.framer.counters().gap_resyncs == 1);
}

void lone_sync_byte_is_dropped_on_gap() {
  Harness h;
  h.feed(std::string(1, '\x4F'));
  h.wait(RunCommandFramer::kGapMs + 1);
  h.feed("?");
  CHECK(h.out.passthrough == "?");   // the 0x4F was not a frame and is not replayed as data
}

// A reply for every shape: the encoder matches the golden frames byte for byte.
void reply_encoder_matches_golden_frames() {
  using orbit::maintenance::FrameType;
  const std::string name = "ENTER-MAINT";
  std::uint8_t out[64]{};
  const std::size_t ack = orbit::maintenance::encode_frame(
      FrameType::Ack, reinterpret_cast<const std::uint8_t*>(name.data()), name.size(), nullptr, 0, out, sizeof out);
  CHECK(ack == 23);
  CHECK(std::string(reinterpret_cast<const char*>(out), ack) == from_hex("4F31410B00000000454E5445522D4D41494E54F836B850"));
  const std::uint8_t code = static_cast<std::uint8_t>(NakReason::NotStopped);
  const std::size_t nak = orbit::maintenance::encode_frame(
      FrameType::Nak, reinterpret_cast<const std::uint8_t*>(name.data()), name.size(), &code, 1, out, sizeof out);
  CHECK(nak == 24);
  CHECK(std::string(reinterpret_cast<const char*>(out), nak) == from_hex("4F314E0B01000000454E5445522D4D41494E541008089424"));
  // Capacity short, name too long, or payload missing: nothing is written, 0 is returned.
  CHECK(orbit::maintenance::encode_frame(FrameType::Ack, reinterpret_cast<const std::uint8_t*>(name.data()),
                                         name.size(), nullptr, 0, out, 22) == 0);
  CHECK(orbit::maintenance::encode_frame(FrameType::Ack, out, 17, nullptr, 0, out, sizeof out) == 0);
  CHECK(orbit::maintenance::encode_frame(FrameType::Nak, nullptr, 0, nullptr, 1, out, sizeof out) == 0);
}

// The INFO text carries the identity, the core module inventory in registry order, the storage
// and scene state, and the maintenance and transport flags; it never truncates.
void info_text_carries_the_device_state() {
  using orbit::InfoFields;
  InfoFields fields;
  fields.firmware = "1.2.3";
  fields.git_sha = "abc1234";
  fields.engine_api = 1;
  fields.board = "adafruit_macropad_rp2040";
  fields.core_modules = orbit::kModuleDescriptions;
  fields.core_module_count = orbit::kModuleDescriptionCount;
  fields.storage_mounted = true;
  fields.scenes = 3;
  fields.names_loaded = true;
  fields.maintenance = true;
  fields.transport_running = false;
  char text[orbit::kInfoTextMaxBytes]{};
  const std::size_t length = orbit::encode_info(fields, text, sizeof text);
  CHECK(length > 0 && length < orbit::kInfoTextMaxBytes);
  const std::string body(text, length);
  CHECK(body.rfind("INFO fw=1.2.3 sha=abc1234 api=1 board=adafruit_macropad_rp2040\n", 0) == 0);
  CHECK(body.find("MODULES count=24 core=note,chords,drum,matrix,note_walk,chord_walk,empty,arp,") != std::string::npos);
  CHECK(body.find(",wander user=\n") != std::string::npos);   // user module list is empty
  CHECK(body.find("STORAGE mounted=1 scenes=3 names_ok=1\n") != std::string::npos);
  CHECK(body.find("STATE maint=1 transport=0\n") != std::string::npos);
  CHECK(body.find("CAPS rename=1\n") != std::string::npos);   // the RENAME frame is served
  CHECK(body.back() == '\n');
  // Bounded: a short capacity gives 0 rather than a truncated text.
  CHECK(orbit::encode_info(fields, text, length - 1) == 0);
  fields.maintenance = false;
  fields.transport_running = true;
  fields.storage_mounted = false;
  const std::size_t second = orbit::encode_info(fields, text, sizeof text);
  const std::string running(text, second);
  CHECK(running.find("STORAGE mounted=0") != std::string::npos);
  CHECK(running.find("STATE maint=0 transport=1\n") != std::string::npos);
}

// A stamped release identity (a 46-character sha with "-dirty", a long prerelease version) with
// all sixteen scenes fits the reply: the text is never refused for capacity, and the bound used
// by the build-time assert covers it.
void info_text_fits_a_stamped_identity() {
  using orbit::InfoFields;
  const char* sha = "0123456789abcdef0123456789abcdef01234567-dirty";
  const char* version = "10.20.30-rc.12+build.20261016.abcdef";
  InfoFields fields;
  fields.firmware = version;
  fields.git_sha = sha;
  fields.engine_api = 4294967295U;
  fields.board = "adafruit_macropad_rp2040";
  fields.core_modules = orbit::kModuleDescriptions;
  fields.core_module_count = orbit::kModuleDescriptionCount;
  fields.storage_mounted = true;
  fields.scenes = 16;
  fields.names_loaded = true;
  fields.maintenance = true;
  fields.transport_running = true;
  char text[orbit::kInfoTextMaxBytes]{};
  const std::size_t length = orbit::encode_info(fields, text, sizeof text);
  CHECK(length > 0 && length <= orbit::kInfoTextMaxBytes);
  const std::size_t bound = orbit::info_text_bound(version, sha, fields.board,
                                                  orbit::kModuleDescriptions, orbit::kModuleDescriptionCount);
  CHECK(bound >= length);
  CHECK(bound <= orbit::kInfoTextMaxBytes);
  const std::string body(text, length);
  CHECK(body.find("INFO fw=10.20.30-rc.12+build.20261016.abcdef sha=0123") != std::string::npos);
  CHECK(body.find("STORAGE mounted=1 scenes=16 names_ok=1\n") != std::string::npos);
  CHECK(body.find("STATE maint=1 transport=1\n") != std::string::npos);
  // The frame that carries the text plus two NAKs fits the outbox.
  std::uint8_t frame[orbit::kInfoTextMaxBytes + 64]{};
  CHECK(orbit::maintenance::encode_frame(orbit::maintenance::FrameType::Data, nullptr, 0,
                                         reinterpret_cast<const std::uint8_t*>(text), length,
                                         frame, sizeof frame) == length + 12);
}

}  // namespace

int main() {
  golden_frames_are_delivered_as_valid_commands();
  info_text_fits_a_stamped_identity();
  token_must_match_exactly();
  bad_checksum_is_never_a_command();
  partial_frame_completes_across_passes();
  garbage_between_frames_resynchronizes();
  question_mark_dump_is_unaffected_by_frames();
  abandoned_partial_frame_replays_and_keeps_question_mark();
  abandoned_frame_tail_is_not_a_command();
  data_requests_are_refused_and_their_payload_discarded();
  payload_on_fixed_frames_is_refused();
  rename_is_refused_in_normal_run();
  unknown_type_and_long_name_are_framing_errors();
  oversize_declared_payload_is_scanned_not_discarded();
  discard_gap_drops_without_replay();
  gap_timeout_is_wrap_safe();
  lone_sync_byte_is_dropped_on_gap();
  reply_encoder_matches_golden_frames();
  info_text_carries_the_device_state();
  if (g_failures == 0) std::printf("t13e_run_commands: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
