#pragma once

#include <cstddef>
#include <cstdint>

#include "persistence/fs_format.hpp"
#include "persistence/scene_service.hpp"

// CDC maintenance protocol (host side: tools/arduino/scene_transfer.py).
//
// Frame layout (little-endian):
//   sync 0x4F 0x31 | type u8 | name_len u8 | payload_len u32 | name | payload | crc32 u32
// The CRC-32 (IEEE, reflected, as zlib.crc32) covers every byte before it.
//
// Requests: 'L' list, 'G' get <name>, 'P' put <name> <payload>, 'X' abort,
// 'F' format <token> (name = kFormatToken exactly, empty payload; erases the
// LittleFS partition and is only accepted while stopped with the storage window
// open).
// Responses: 'D' data (list text or file bytes), 'A' ack (after verified
// commit or verified format), 'N' nak (name echo; payload = one NakReason byte).
//
// Names are an exact allowlist: Scene01..Scene10, _activescene, user_settings.
// Payload bounds are per file. Transfers write only to the temp file of the
// target; the target changes only after the temp file is closed, verified by
// decoding, and renamed. Maintenance requests are refused unless the gate
// reports stopped. Protocol state is fixed-size; nothing is buffered beyond
// one 64-byte chunk.
namespace orbit {
namespace maintenance {

constexpr std::uint8_t kSync0 = 0x4F;
constexpr std::uint8_t kSync1 = 0x31;
constexpr std::size_t kHeaderBytes = 8;   // sync(2) + type + name_len + payload_len(4)
constexpr std::size_t kNameMax = 16;      // longest allowlisted name is "user_settings" (13)
constexpr std::size_t kChunkBytes = 64;
constexpr std::size_t kCrcBytes = 4;
constexpr std::size_t kListBytes = 256;
// Confirmation token for FORMAT. The name field must equal this text exactly (8 bytes),
// with an empty payload and a valid CRC. Existing frame types never format.
constexpr char kFormatToken[] = "ERASE-FS";
constexpr std::size_t kFormatTokenLength = 8;

enum class FrameType : std::uint8_t {
  List = 'L', Get = 'G', Put = 'P', Abort = 'X', Format = 'F', Ack = 'A', Nak = 'N', Data = 'D'
};

enum class NakReason : std::uint8_t {
  None = 0,
  BadFrame = 1,
  BadChecksum = 2,
  Oversize = 3,
  DisallowedName = 4,
  Busy = 5,          // not stopped: maintenance refused during performance
  StorageFull = 6,
  StorageError = 7,
  InvalidContent = 8,
  NotFound = 9,
  BadToken = 10,     // FORMAT name is not the confirmation token
  FormatFailed = 11, // partition could not be formatted (or the window was closed)
  MountFailed = 12,  // formatted, but the filesystem did not mount afterwards
  VerifyFailed = 13  // mounted, but the probe or stamp did not verify
};

// CRC-32 with the zlib/IEEE polynomial. Start with crc32_init, feed bytes with
// crc32_update, and finish with crc32_final.
constexpr std::uint32_t crc32_init() noexcept { return 0xFFFFFFFFU; }
std::uint32_t crc32_update(std::uint32_t state, std::uint8_t byte) noexcept;
constexpr std::uint32_t crc32_final(std::uint32_t state) noexcept { return ~state; }

// Exact allowlist. Returns false for any other text (including path separators,
// lowercase variants, Scene00/Scene11 and the temp/backup names).
bool resolve_name(const char* name, std::size_t length, SceneFileId& id) noexcept;
// Writes the canonical name; returns its length, or 0 when capacity is short.
std::size_t format_name(SceneFileId id, char* output, std::size_t capacity) noexcept;
// Largest payload accepted or served for a file. Scene: kSceneMaxBytes;
// active pointer: 4 bytes ("10\n" plus slack); user settings: kUserSettingsMaxBytes.
std::size_t max_payload(SceneFileId id) noexcept;

struct MaintenanceOutput {
  void* context = nullptr;
  bool (*write)(void*, const std::uint8_t*, std::size_t) = nullptr;
};
struct MaintenanceGate {
  void* context = nullptr;
  bool (*stopped)(void*) = nullptr;
};
// FORMAT callback. Runs format_verified() through the storage adapter; the adapter also
// owns the mounted state and reports it honestly on every outcome.
struct MaintenanceFormatPort {
  void* context = nullptr;
  fsformat::FormatResult (*format)(void*) = nullptr;
};
struct MaintenanceCounters {
  std::uint32_t accepted = 0;        // committed PUTs (ACK sent)
  std::uint32_t served = 0;          // GET/LIST answered with data
  std::uint32_t rejected = 0;        // NAK sent for any reason
  std::uint32_t framing_errors = 0;
  std::uint32_t checksum_errors = 0;
  std::uint32_t oversize = 0;
  std::uint32_t disallowed = 0;
  std::uint32_t busy = 0;
  std::uint32_t storage_full = 0;
  std::uint32_t storage_errors = 0;
  std::uint32_t invalid_content = 0;
  std::uint32_t aborted = 0;         // transfers discarded by abort()
  std::uint32_t output_failures = 0; // output port refused a response byte
  std::uint32_t formats = 0;         // FORMAT verified and ACKed
  std::uint32_t format_failures = 0; // FORMAT NAKed after the port was called
};

class MaintenanceSession {
 public:
  // `service` lends its staging SceneDto for SCN1 verification (SceneService::lend_staging).
  // Verification of a Scene PUT is refused with NakReason::Busy while the service is busy.
  // No second SceneDto exists: the two are never used at the same time.
  MaintenanceSession(const SceneStoragePort& storage, const SceneSchemaResolver& schema,
                     SceneService& service, MaintenanceOutput output,
                     MaintenanceGate gate) noexcept;

  // Consumes one received byte. Responses are written through the output port.
  void feed(std::uint8_t byte) noexcept;
  // Called by the application on disconnect or inactivity timeout. Closes and
  // removes any temp file of an open PUT; the target file is never touched.
  void abort() noexcept;

  // Installs the FORMAT callback. Without it, FORMAT is NAKed with FormatFailed.
  void set_format(MaintenanceFormatPort port) noexcept { format_ = port; }

  const MaintenanceCounters& counters() const noexcept { return counters_; }
  bool transfer_open() const noexcept { return transfer_open_; }
  bool mid_frame() const noexcept { return state_ != State::Hunt; }

 private:
  enum class State : std::uint8_t { Hunt, Header, Name, Payload, Checksum, Discard };

  void reset_frame() noexcept;
  void dispatch() noexcept;
  void begin_put(SceneFileId id) noexcept;
  void abort_after_frame() noexcept;
  void finish_frame() noexcept;
  void flush_chunk() noexcept;
  void reject(NakReason reason) noexcept;
  void discard_transfer() noexcept;
  bool close_temp_writer() noexcept;
  NakReason verify_temp() noexcept;
  bool stopped() const noexcept;
  NakReason commit_temp() noexcept;
  void do_list() noexcept;
  void do_get() noexcept;
  bool file_present(SceneFileId id, bool& present) noexcept;

  void send_begin(FrameType type, const std::uint8_t* name, std::size_t name_length,
                  std::uint32_t payload_length) noexcept;
  void send_bytes(const std::uint8_t* data, std::size_t length) noexcept;
  void send_end() noexcept;

  SceneStoragePort storage_{};
  SceneSchemaResolver schema_{};
  MaintenanceFormatPort format_{};
  SceneService* service_ = nullptr;
  MaintenanceOutput output_{};
  MaintenanceGate gate_{};
  MaintenanceCounters counters_{};

  State state_ = State::Hunt;
  bool sync0_ = false;
  std::uint32_t crc_ = crc32_init();   // running CRC of the current frame
  std::uint8_t header_[6]{};
  std::uint8_t header_count_ = 0;
  std::uint8_t type_ = 0;
  std::uint8_t name_length_ = 0;
  std::uint8_t name_count_ = 0;
  std::uint8_t name_[kNameMax]{};
  std::uint32_t payload_length_ = 0;
  std::uint32_t remaining_ = 0;       // payload bytes (Payload) or bytes to skip (Discard)
  std::uint8_t trailer_[kCrcBytes]{};
  std::uint8_t trailer_count_ = 0;

  SceneWriter writer_{};
  SceneFileId target_{};
  SceneFileId temp_{};
  SceneFileId backup_{};
  bool target_valid_ = false;
  bool transfer_open_ = false;
  NakReason pending_ = NakReason::None;
  std::uint8_t chunk_[kChunkBytes]{};
  std::size_t chunk_count_ = 0;
  std::uint32_t emit_crc_ = crc32_init();
  // Payload-only checksum and length: the read-back of the temp file must match
  // these, so a write the storage silently truncated is never acknowledged.
  std::uint32_t payload_crc_ = crc32_init();
  std::uint32_t payload_count_ = 0;
};

}  // namespace maintenance
}  // namespace orbit
