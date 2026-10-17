#pragma once

#include <cstddef>
#include <cstdint>

#include "maintenance_protocol.hpp"
#include "scene_schema.hpp"

// Normal-run framer for the maintenance commands (host side: tools/arduino/scene_transfer.py).
//
// While the engine runs, every received byte goes through feed(). Only complete frames with
// the sync bytes 0x4F 0x31 are consumed; every other byte is passed through (the '?' dump).
// Commands that reach the application are:
//   'I' info      (empty name, empty payload)                      -> reply in the application
//   'M' enter     (name exactly ENTER-MAINT, empty payload)        -> the application decides
//   'E' exit      (name exactly EXIT-MAINT) in normal run           -> NAK NotInMaintenance
// Any other valid request type (L, G, P, X, F, R) is refused early with NotInMaintenance and its
// bytes are discarded. An unknown type or a name longer than kNameMax is a framing error:
// BadFrame, and the framer resynchronizes at the next sync bytes. A frame that carries a
// payload where none belongs is refused with BadFrame. CRC-32 is checked for every frame that
// is not refused early, and a mismatch is reported as BadChecksum. A frame whose name is not
// its token is BadToken, so a stray frame with a wrong name never triggers a command.
//
// Bounds: no heap. The framer holds at most one frame's sync, header and name (28 bytes) for
// replay. A frame that stops arriving for kGapMs is abandoned: its bytes after the sync are
// replayed through the scanner, so a '?' that followed a partial frame still reaches the dump.
// A discarded payload is at most kMaxDiscardBytes; a longer declared payload is not discarded
// (its bytes are scanned), and a discard that stops arriving for kGapMs is dropped without
// replay, since payload bytes are data.
namespace orbit {
namespace maintenance {

struct RunCommand {
  std::uint8_t type = 0;            // raw frame type byte (a FrameType value for valid frames)
  std::uint8_t name_length = 0;     // 0..kNameMax; 0 when no name is echoed
  std::uint8_t name[kNameMax]{};
  // None: a valid Info, Enter or Exit frame. Otherwise the reason to NAK the frame with.
  NakReason verdict = NakReason::None;
};

// Callbacks. The framer holds no references, so a sink is a plain value.
struct RunSink {
  void* context = nullptr;
  void (*command)(void* context, const RunCommand& command) noexcept = nullptr;
  void (*passthrough)(void* context, std::uint8_t byte) noexcept = nullptr;
};

struct RunCounters {
  std::uint32_t commands = 0;        // complete frames delivered to the sink
  std::uint32_t refused = 0;         // requests refused early (NotInMaintenance)
  std::uint32_t bad_frames = 0;      // framing errors and payloads on frames that carry none
  std::uint32_t bad_checksum = 0;
  std::uint32_t bad_token = 0;
  std::uint32_t gap_resyncs = 0;     // partial frames abandoned after kGapMs (bytes replayed)
  std::uint32_t discard_drops = 0;   // discards abandoned after kGapMs (bytes dropped)
};

class RunCommandFramer {
 public:
  static constexpr std::uint32_t kGapMs = 250;
  static constexpr std::size_t kNameBytes = kNameMax;
  static constexpr std::size_t kRawBytes = 2 + 6 + kNameBytes + kCrcBytes;   // sync, header, name, CRC
  static constexpr std::size_t kMaxDiscardBytes = kSceneMaxBytes + 64;

  // Consumes one received byte at time now_ms (milliseconds, wrap-safe).
  void feed(std::uint8_t byte, std::uint32_t now_ms, const RunSink& sink) noexcept;
  // Called on every pass: abandons a partial frame or discard that has stopped arriving.
  void poll(std::uint32_t now_ms, const RunSink& sink) noexcept;

  const RunCounters& counters() const noexcept { return counters_; }
  bool mid_frame() const noexcept { return state_ != State::Hunt || sync0_; }
  // Drops any partial frame or discard without replay (used when maintenance starts or ends, so a
  // frame begun before the state change is never completed after it).
  void resync() noexcept { to_hunt(); }

 private:
  enum class State : std::uint8_t { Hunt, Header, Name, Trailer, Discard };

  void hunt(std::uint8_t byte, std::uint32_t now_ms, const RunSink& sink) noexcept;
  void deliver(NakReason verdict, const RunSink& sink) noexcept;
  void discard_or_hunt(std::uint64_t declared, std::uint32_t now_ms) noexcept;
  void finish_trailer(const RunSink& sink) noexcept;
  void to_hunt() noexcept;
  void replay(std::uint32_t now_ms, const RunSink& sink) noexcept;

  State state_ = State::Hunt;
  bool sync0_ = false;                // a 0x4F that may start a frame
  std::uint32_t last_ms_ = 0;         // time of the last byte of the current frame or discard
  std::uint32_t crc_ = 0;
  std::uint8_t raw_[kRawBytes]{};     // every byte of the frame so far (sync included), for replay
  std::uint8_t raw_count_ = 0;
  std::uint8_t header_[6]{};          // type, name length, payload length (LE32)
  std::uint8_t header_count_ = 0;
  std::uint8_t name_count_ = 0;
  std::uint8_t trailer_[kCrcBytes]{};
  std::uint8_t trailer_count_ = 0;
  std::uint32_t payload_length_ = 0;
  std::uint32_t remaining_ = 0;       // Discard: bytes still to drop
  RunCommand command_{};
  RunCounters counters_{};
};

}  // namespace maintenance
}  // namespace orbit
