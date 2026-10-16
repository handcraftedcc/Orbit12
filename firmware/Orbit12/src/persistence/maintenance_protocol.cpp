#include "persistence/maintenance_protocol.hpp"

#include <cstring>

#include "persistence/fs_format.hpp"
#include "persistence/user_settings.hpp"

namespace orbit {
namespace maintenance {
namespace {

constexpr std::uint32_t kCrcPolynomial = 0xEDB88320U;
constexpr char kActiveName[] = "_activescene";
constexpr std::size_t kActiveNameLength = sizeof(kActiveName) - 1;
constexpr char kUserName[] = "user_settings";
constexpr std::size_t kUserNameLength = sizeof(kUserName) - 1;
constexpr std::size_t kSceneNameLength = 7;   // "Scene01"
constexpr std::uint8_t kPointerMaxBytes = 4;

bool valid_type(std::uint8_t type) noexcept {
  return type == static_cast<std::uint8_t>(FrameType::List) ||
         type == static_cast<std::uint8_t>(FrameType::Get) ||
         type == static_cast<std::uint8_t>(FrameType::Put) ||
         type == static_cast<std::uint8_t>(FrameType::Abort) ||
         type == static_cast<std::uint8_t>(FrameType::Format);
}

NakReason format_nak(fsformat::FormatResult result) noexcept {
  switch (result) {
    case fsformat::FormatResult::Ok: return NakReason::None;
    case fsformat::FormatResult::FormatFailed: return NakReason::FormatFailed;
    case fsformat::FormatResult::MountFailed: return NakReason::MountFailed;
    case fsformat::FormatResult::VerifyFailed: return NakReason::VerifyFailed;
  }
  return NakReason::FormatFailed;
}

SceneFileId temp_of(SceneFileId target) noexcept {
  switch (target.kind) {
    case SceneFileKind::Scene: return {SceneFileKind::SceneTemp, target.scene};
    case SceneFileKind::ActivePointer: return {SceneFileKind::ActiveTemp, 0};
    default: return {SceneFileKind::UserSettingsTemp, 0};
  }
}

SceneFileId backup_of(SceneFileId target) noexcept {
  switch (target.kind) {
    case SceneFileKind::Scene: return {SceneFileKind::SceneBackup, target.scene};
    case SceneFileKind::ActivePointer: return {SceneFileKind::ActiveBackup, 0};
    default: return {SceneFileKind::UserSettingsBackup, 0};
  }
}

// Same grammar as SceneService::read_pointer: "N\n" or "N\r\n", N in 1..10.
bool valid_pointer(const char* text, std::size_t length) noexcept {
  auto digit = [](char c) { return c >= '1' && c <= '9'; };
  if (length == 2 && digit(text[0]) && text[1] == '\n') return true;
  if (length == 3 && digit(text[0]) && text[1] == '\r' && text[2] == '\n') return true;
  if (length == 3 && text[0] == '1' && text[1] == '0' && text[2] == '\n') return true;
  return length == 4 && text[0] == '1' && text[1] == '0' && text[2] == '\r' && text[3] == '\n';
}

}  // namespace

std::uint32_t crc32_update(std::uint32_t state, std::uint8_t byte) noexcept {
  state ^= byte;
  for (int bit = 0; bit < 8; ++bit)
    state = (state & 1U) ? (state >> 1) ^ kCrcPolynomial : state >> 1;
  return state;
}

bool resolve_name(const char* name, std::size_t length, SceneFileId& id) noexcept {
  if (name == nullptr) return false;
  if (length == kSceneNameLength && std::memcmp(name, "Scene", 5) == 0) {
    const char tens = name[5], ones = name[6];
    if (tens < '0' || tens > '9' || ones < '0' || ones > '9') return false;
    const int number = (tens - '0') * 10 + (ones - '0');
    if (number < 1 || number > 10) return false;
    id = {SceneFileKind::Scene, static_cast<std::uint8_t>(number - 1)};
    return true;
  }
  if (length == kActiveNameLength && std::memcmp(name, kActiveName, kActiveNameLength) == 0) {
    id = {SceneFileKind::ActivePointer, 0};
    return true;
  }
  if (length == kUserNameLength && std::memcmp(name, kUserName, kUserNameLength) == 0) {
    id = {SceneFileKind::UserSettings, 0};
    return true;
  }
  return false;
}

std::size_t format_name(SceneFileId id, char* output, std::size_t capacity) noexcept {
  if (output == nullptr) return 0;
  if (id.kind == SceneFileKind::Scene) {
    if (capacity < kSceneNameLength + 1 || id.scene >= 10) return 0;
    const int number = id.scene + 1;
    std::memcpy(output, "Scene", 5);
    output[5] = static_cast<char>('0' + number / 10);
    output[6] = static_cast<char>('0' + number % 10);
    output[7] = '\0';
    return kSceneNameLength;
  }
  if (id.kind == SceneFileKind::ActivePointer) {
    if (capacity < kActiveNameLength + 1) return 0;
    std::memcpy(output, kActiveName, kActiveNameLength + 1);
    return kActiveNameLength;
  }
  if (id.kind == SceneFileKind::UserSettings) {
    if (capacity < kUserNameLength + 1) return 0;
    std::memcpy(output, kUserName, kUserNameLength + 1);
    return kUserNameLength;
  }
  return 0;
}

std::size_t max_payload(SceneFileId id) noexcept {
  if (id.kind == SceneFileKind::Scene) return kSceneMaxBytes;
  if (id.kind == SceneFileKind::ActivePointer) return kPointerMaxBytes;
  if (id.kind == SceneFileKind::UserSettings) return kUserSettingsMaxBytes;
  return 0;
}

MaintenanceSession::MaintenanceSession(const SceneStoragePort& storage,
                                       const SceneSchemaResolver& schema, SceneService& service,
                                       MaintenanceOutput output, MaintenanceGate gate) noexcept
    : storage_(storage), schema_(schema), service_(&service), output_(output), gate_(gate) {}

void MaintenanceSession::reset_frame() noexcept {
  state_ = State::Hunt;
  sync0_ = false;
  header_count_ = 0;
  name_count_ = 0;
  trailer_count_ = 0;
  remaining_ = 0;
  crc_ = crc32_init();
  pending_ = NakReason::None;
}

void MaintenanceSession::feed(std::uint8_t byte) noexcept {
  switch (state_) {
    case State::Hunt:
      if (!sync0_) {
        sync0_ = byte == kSync0;
      } else if (byte == kSync1) {
        sync0_ = false;
        crc_ = crc32_update(crc32_update(crc32_init(), kSync0), kSync1);
        header_count_ = 0;
        state_ = State::Header;
      } else {
        sync0_ = byte == kSync0;
      }
      return;
    case State::Header:
      crc_ = crc32_update(crc_, byte);
      header_[header_count_++] = byte;
      if (header_count_ == sizeof(header_)) {
        type_ = header_[0];
        name_length_ = header_[1];
        payload_length_ = static_cast<std::uint32_t>(header_[2]) |
                          (static_cast<std::uint32_t>(header_[3]) << 8) |
                          (static_cast<std::uint32_t>(header_[4]) << 16) |
                          (static_cast<std::uint32_t>(header_[5]) << 24);
        if (!valid_type(type_) || name_length_ > kNameMax) {
          ++counters_.framing_errors;
          name_length_ = 0;  // no trustworthy name to echo
          reject(NakReason::BadFrame);
          reset_frame();   // payload length cannot be trusted; resynchronize on sync bytes
          return;
        }
        name_count_ = 0;
        if (name_length_ == 0) dispatch();
        else state_ = State::Name;
      }
      return;
    case State::Name:
      crc_ = crc32_update(crc_, byte);
      name_[name_count_++] = byte;
      if (name_count_ == name_length_) dispatch();
      return;
    case State::Payload:
      crc_ = crc32_update(crc_, byte);
      payload_crc_ = crc32_update(payload_crc_, byte);
      ++payload_count_;
      if (pending_ == NakReason::None) {
        chunk_[chunk_count_++] = byte;
        if (chunk_count_ == kChunkBytes) flush_chunk();
      }
      if (--remaining_ == 0) {
        flush_chunk();
        trailer_count_ = 0;
        state_ = State::Checksum;
      }
      return;
    case State::Checksum:
      trailer_[trailer_count_++] = byte;
      if (trailer_count_ == kCrcBytes) finish_frame();
      return;
    case State::Discard:
      if (remaining_ > 0) {
        --remaining_;
        return;
      }
      if (++trailer_count_ == kCrcBytes) reset_frame();
      return;
  }
}

void MaintenanceSession::dispatch() noexcept {
  SceneFileId id{};
  const bool named = resolve_name(reinterpret_cast<const char*>(name_), name_length_, id);
  const bool stopped = gate_.stopped != nullptr && gate_.stopped(gate_.context);
  NakReason nak = NakReason::None;
  switch (static_cast<FrameType>(type_)) {
    case FrameType::List:
      if (!stopped) nak = NakReason::Busy;
      else if (payload_length_ != 0 || name_length_ != 0) nak = NakReason::BadFrame;
      break;
    case FrameType::Get:
      if (!named) nak = NakReason::DisallowedName;
      else if (!stopped) nak = NakReason::Busy;
      else if (payload_length_ != 0) nak = NakReason::BadFrame;
      break;
    case FrameType::Abort:
      if (payload_length_ != 0) nak = NakReason::BadFrame;
      break;
    case FrameType::Format:
      // The token is checked before the gate: a stray or wrong frame is never a format.
      if (name_length_ != kFormatTokenLength ||
          std::memcmp(name_, kFormatToken, kFormatTokenLength) != 0)
        nak = NakReason::BadToken;
      else if (payload_length_ != 0) nak = NakReason::BadFrame;
      else if (!stopped) nak = NakReason::Busy;
      break;
    case FrameType::Put:
      if (!named) nak = NakReason::DisallowedName;
      else if (!stopped) nak = NakReason::Busy;
      else if (payload_length_ > max_payload(id)) nak = NakReason::Oversize;
      break;
    default:
      nak = NakReason::BadFrame;
      break;
  }
  if (nak != NakReason::None) {
    reject(nak);
    remaining_ = payload_length_;
    trailer_count_ = 0;
    state_ = State::Discard;
    return;
  }
  if (static_cast<FrameType>(type_) == FrameType::Put) {
    begin_put(id);
    return;
  }
  trailer_count_ = 0;
  state_ = State::Checksum;
}

void MaintenanceSession::begin_put(SceneFileId id) noexcept {
  target_ = id;
  temp_ = temp_of(id);
  backup_ = backup_of(id);
  target_valid_ = true;
  pending_ = NakReason::None;
  chunk_count_ = 0;
  payload_crc_ = crc32_init();
  payload_count_ = 0;
  // A stale temp file from an earlier interrupted transfer is removed first.
  const auto removed = storage_.remove(storage_.context, temp_);
  if (removed != SceneStorageStatus::Ok && removed != SceneStorageStatus::Missing) {
    discard_transfer();
    reject(NakReason::StorageError);
    remaining_ = payload_length_;
    trailer_count_ = 0;
    state_ = State::Discard;
    return;
  }
  if (storage_.open_write(storage_.context, temp_, writer_) != SceneStorageStatus::Ok) {
    discard_transfer();
    reject(NakReason::StorageError);
    remaining_ = payload_length_;
    trailer_count_ = 0;
    state_ = State::Discard;
    return;
  }
  transfer_open_ = true;
  if (payload_length_ == 0) {
    trailer_count_ = 0;
    state_ = State::Checksum;
  } else {
    remaining_ = payload_length_;
    state_ = State::Payload;
  }
}

bool MaintenanceSession::stopped() const noexcept {
  return gate_.stopped != nullptr && gate_.stopped(gate_.context);
}

void MaintenanceSession::flush_chunk() noexcept {
  if (chunk_count_ == 0) return;
  // Re-checked at every flush: the gate can close while a long payload is arriving.
  if (pending_ == NakReason::None && !stopped()) pending_ = NakReason::Busy;
  if (pending_ == NakReason::None) {
    const SceneWriteStatus status =
        writer_.write == nullptr ? SceneWriteStatus::Error
                                 : writer_.write(writer_.context,
                                                 reinterpret_cast<const char*>(chunk_),
                                                 chunk_count_);
    if (status == SceneWriteStatus::Full) pending_ = NakReason::StorageFull;
    else if (status != SceneWriteStatus::Written) pending_ = NakReason::StorageError;
  }
  chunk_count_ = 0;
}

bool MaintenanceSession::close_temp_writer() noexcept {
  if (!transfer_open_) return true;
  transfer_open_ = false;
  return storage_.close(storage_.context, temp_) == SceneStorageStatus::Ok;
}

void MaintenanceSession::discard_transfer() noexcept {
  if (transfer_open_) {
    transfer_open_ = false;
    storage_.close(storage_.context, temp_);
  }
  if (target_valid_) {
    // The target file is never touched here; only the temp file is removed.
    storage_.remove(storage_.context, temp_);
    target_valid_ = false;
  }
}

void MaintenanceSession::abort() noexcept {
  if (transfer_open_ || target_valid_) {
    discard_transfer();
    ++counters_.aborted;
  }
  reset_frame();
}

void MaintenanceSession::finish_frame() noexcept {
  const std::uint32_t expected = crc32_final(crc_);
  const std::uint32_t received = static_cast<std::uint32_t>(trailer_[0]) |
                                 (static_cast<std::uint32_t>(trailer_[1]) << 8) |
                                 (static_cast<std::uint32_t>(trailer_[2]) << 16) |
                                 (static_cast<std::uint32_t>(trailer_[3]) << 24);
  const auto type = static_cast<FrameType>(type_);
  if (received != expected) {
    ++counters_.checksum_errors;
    discard_transfer();
    reject(NakReason::BadChecksum);
    reset_frame();
    return;
  }
  switch (type) {
    case FrameType::List:
      do_list();
      break;
    case FrameType::Get:
      do_get();
      break;
    case FrameType::Abort:
      abort_after_frame();
      break;
    case FrameType::Format: {
      // Re-checked at the commit point: the gate may have closed during the frame.
      if (!stopped()) {
        reject(NakReason::Busy);
        break;
      }
      discard_transfer();  // an open PUT is discarded first; its target is untouched
      const fsformat::FormatResult result =
          format_.format == nullptr ? fsformat::FormatResult::FormatFailed
                                    : format_.format(format_.context);
      if (result != fsformat::FormatResult::Ok) {
        ++counters_.format_failures;
        reject(format_nak(result));
        break;
      }
      ++counters_.formats;
      send_begin(FrameType::Ack, name_, name_length_, 0);
      send_end();
      break;
    }
    case FrameType::Put: {
      if (pending_ == NakReason::None && !stopped()) pending_ = NakReason::Busy;
      if (pending_ != NakReason::None) {
        discard_transfer();
        reject(pending_);
        break;
      }
      if (!close_temp_writer()) {
        discard_transfer();
        reject(NakReason::StorageError);
        break;
      }
      const NakReason verified = verify_temp();
      if (verified != NakReason::None) {
        discard_transfer();
        reject(verified);
        break;
      }
      const NakReason committed = commit_temp();
      if (committed != NakReason::None) {
        discard_transfer();
        reject(committed);
        break;
      }
      target_valid_ = false;
      ++counters_.accepted;
      send_begin(FrameType::Ack, name_, name_length_, 0);
      send_end();
      break;
    }
    default:
      reject(NakReason::BadFrame);
      break;
  }
  reset_frame();
}

void MaintenanceSession::abort_after_frame() noexcept {
  if (transfer_open_ || target_valid_) {
    discard_transfer();
    ++counters_.aborted;
  }
  send_begin(FrameType::Ack, name_, name_length_, 0);
  send_end();
}

NakReason MaintenanceSession::verify_temp() noexcept {
  SceneReader reader{};
  if (storage_.open_read(storage_.context, temp_, reader) != SceneStorageStatus::Ok)
    return NakReason::StorageError;
  // Byte-level read-back first: length and CRC must equal what was received.
  std::uint32_t stored_count = 0;
  std::uint32_t stored_crc = crc32_init();
  SceneReadStatus pass_status = SceneReadStatus::Byte;
  char pass_byte = 0;
  while ((pass_status = reader.read(reader.context, pass_byte)) == SceneReadStatus::Byte) {
    ++stored_count;
    stored_crc = crc32_update(stored_crc, static_cast<std::uint8_t>(pass_byte));
  }
  storage_.close(storage_.context, temp_);
  if (pass_status != SceneReadStatus::End) return NakReason::StorageError;
  if (stored_count != payload_count_ || crc32_final(stored_crc) != crc32_final(payload_crc_))
    return NakReason::StorageError;   // storage accepted the write but kept different bytes

  if (storage_.open_read(storage_.context, temp_, reader) != SceneStorageStatus::Ok)
    return NakReason::StorageError;
  NakReason result = NakReason::None;
  switch (target_.kind) {
    case SceneFileKind::Scene: {
      SceneDto* staging = service_->lend_staging();
      if (staging == nullptr) {
        result = NakReason::Busy;   // scene operation in progress: the staging object is not ours
        break;
      }
      const SceneResult decoded = decode_scene(reader, schema_, *staging);
      if (decoded == SceneResult::IoError) result = NakReason::StorageError;
      else if (decoded != SceneResult::Ok) result = NakReason::InvalidContent;
      break;
    }
    case SceneFileKind::ActivePointer: {
      char text[kPointerMaxBytes + 1]{};
      std::size_t count = 0;
      SceneReadStatus status = SceneReadStatus::Byte;
      char byte = 0;
      while (count < kPointerMaxBytes && (status = reader.read(reader.context, byte)) ==
                                            SceneReadStatus::Byte)
        text[count++] = byte;
      if (status == SceneReadStatus::Error || status == SceneReadStatus::Missing)
        result = NakReason::StorageError;
      else if (status != SceneReadStatus::End || !valid_pointer(text, count))
        result = NakReason::InvalidContent;
      break;
    }
    default: {
      char text[kUserSettingsMaxBytes + 1]{};
      std::size_t count = 0;
      SceneReadStatus status = SceneReadStatus::Byte;
      char byte = 0;
      while (count < sizeof(text) && (status = reader.read(reader.context, byte)) ==
                                       SceneReadStatus::Byte)
        text[count++] = byte;
      if (status == SceneReadStatus::Error || status == SceneReadStatus::Missing) {
        result = NakReason::StorageError;
      } else if (status != SceneReadStatus::End || count > kUserSettingsMaxBytes) {
        result = NakReason::InvalidContent;
      } else {
        UserSettings settings{};
        if (parse_user_settings(text, count, settings) != UserSettingsResult::Ok)
          result = NakReason::InvalidContent;
      }
      break;
    }
  }
  if (storage_.close(storage_.context, temp_) != SceneStorageStatus::Ok &&
      result == NakReason::None)
    result = NakReason::StorageError;
  return result;
}

NakReason MaintenanceSession::commit_temp() noexcept {
  bool exists = false;
  if (!file_present(target_, exists)) return NakReason::StorageError;
  // Rename the old target aside first; on any later failure it is restored.
  if (exists && storage_.rename(storage_.context, target_, backup_) != SceneStorageStatus::Ok)
    return NakReason::StorageError;
  if (storage_.rename(storage_.context, temp_, target_) != SceneStorageStatus::Ok) {
    // Restores the previous file. If this also fails, boot-time recovery
    // (SceneService::recover_file) restores it from the backup name.
    if (exists) storage_.rename(storage_.context, backup_, target_);
    return NakReason::StorageError;
  }
  if (exists) {
    // A retained backup is cleaned by the next recovery pass; the commit stands.
    storage_.remove(storage_.context, backup_);
  }
  return NakReason::None;
}

bool MaintenanceSession::file_present(SceneFileId id, bool& present) noexcept {
  present = false;
  const auto status = storage_.exists(storage_.context, id, present);
  if (status == SceneStorageStatus::Missing) {
    present = false;
    return true;
  }
  return status == SceneStorageStatus::Ok;
}

void MaintenanceSession::do_list() noexcept {
  char text[kListBytes]{};
  std::size_t length = 0;
  auto append_entry = [&](SceneFileId id) -> bool {
    char name[kNameMax]{};
    const std::size_t name_length = format_name(id, name, sizeof(name));
    bool present = false;
    if (name_length == 0 || !file_present(id, present)) return false;
    if (length + name_length + 3 > sizeof(text)) return false;
    std::memcpy(text + length, name, name_length);
    length += name_length;
    text[length++] = ' ';
    text[length++] = present ? '1' : '0';
    text[length++] = '\n';
    return true;
  };
  for (std::uint8_t scene = 0; scene < 10; ++scene)
    if (!append_entry({SceneFileKind::Scene, scene})) { reject(NakReason::StorageError); return; }
  if (!append_entry({SceneFileKind::ActivePointer, 0}) ||
      !append_entry({SceneFileKind::UserSettings, 0})) {
    reject(NakReason::StorageError);
    return;
  }
  send_begin(FrameType::Data, nullptr, 0, static_cast<std::uint32_t>(length));
  send_bytes(reinterpret_cast<const std::uint8_t*>(text), length);
  send_end();
  ++counters_.served;
}

void MaintenanceSession::do_get() noexcept {
  SceneFileId id{};
  if (!resolve_name(reinterpret_cast<const char*>(name_), name_length_, id)) {
    reject(NakReason::DisallowedName);
    return;
  }
  bool present = false;
  if (!file_present(id, present)) { reject(NakReason::StorageError); return; }
  if (!present) { reject(NakReason::NotFound); return; }

  // Pass 1 bounds the file and fixes the frame length before any byte is sent.
  SceneReader reader{};
  if (storage_.open_read(storage_.context, id, reader) != SceneStorageStatus::Ok) {
    reject(NakReason::StorageError);
    return;
  }
  const std::size_t limit = max_payload(id);
  std::size_t count = 0;
  bool bad = false;
  SceneReadStatus status = SceneReadStatus::Byte;
  char byte = 0;
  while ((status = reader.read(reader.context, byte)) == SceneReadStatus::Byte) {
    if (++count > limit) { bad = true; break; }
  }
  if (storage_.close(storage_.context, id) != SceneStorageStatus::Ok) bad = true;
  if (bad || status != SceneReadStatus::End) {
    reject(NakReason::StorageError);
    return;
  }

  // Pass 2 streams the bytes through a single chunk.
  if (storage_.open_read(storage_.context, id, reader) != SceneStorageStatus::Ok) {
    reject(NakReason::StorageError);
    return;
  }
  send_begin(FrameType::Data, name_, name_length_, static_cast<std::uint32_t>(count));
  std::uint8_t buffer[kChunkBytes]{};
  std::size_t filled = 0;
  std::size_t sent = 0;
  bool changed = false;
  while ((status = reader.read(reader.context, byte)) == SceneReadStatus::Byte) {
    if (sent + filled == count) { changed = true; break; }
    buffer[filled++] = static_cast<std::uint8_t>(byte);
    if (filled == kChunkBytes) {
      send_bytes(buffer, filled);
      sent += filled;
      filled = 0;
    }
  }
  if (filled != 0) {
    send_bytes(buffer, filled);
    sent += filled;
  }
  storage_.close(storage_.context, id);
  send_end();
  if (changed || status != SceneReadStatus::End || sent != count) ++counters_.framing_errors;
  else ++counters_.served;
}

void MaintenanceSession::reject(NakReason reason) noexcept {
  ++counters_.rejected;
  switch (reason) {
    case NakReason::BadFrame: break;
    case NakReason::BadChecksum: break;
    case NakReason::Oversize: ++counters_.oversize; break;
    case NakReason::DisallowedName: ++counters_.disallowed; break;
    case NakReason::Busy: ++counters_.busy; break;
    case NakReason::StorageFull: ++counters_.storage_full; break;
    case NakReason::StorageError: ++counters_.storage_errors; break;
    case NakReason::InvalidContent: ++counters_.invalid_content; break;
    case NakReason::NotFound: break;
    case NakReason::BadToken: break;
    case NakReason::FormatFailed: break;
    case NakReason::MountFailed: break;
    case NakReason::VerifyFailed: break;
    case NakReason::None: break;
  }
  const std::uint8_t code = static_cast<std::uint8_t>(reason);
  const std::size_t name_length = name_length_ <= kNameMax ? name_length_ : 0;
  send_begin(FrameType::Nak, name_, name_length, 1);
  send_bytes(&code, 1);
  send_end();
}

void MaintenanceSession::send_begin(FrameType type, const std::uint8_t* name,
                                    std::size_t name_length,
                                    std::uint32_t payload_length) noexcept {
  const std::uint8_t header[kHeaderBytes] = {
      kSync0, kSync1, static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(name_length),
      static_cast<std::uint8_t>(payload_length), static_cast<std::uint8_t>(payload_length >> 8),
      static_cast<std::uint8_t>(payload_length >> 16),
      static_cast<std::uint8_t>(payload_length >> 24)};
  emit_crc_ = crc32_init();
  // Output failures are counted; the CRC is still computed so framing stays consistent.
  if (!output_.write || !output_.write(output_.context, header, sizeof(header)))
    ++counters_.output_failures;
  for (std::size_t i = 0; i < sizeof(header); ++i) emit_crc_ = crc32_update(emit_crc_, header[i]);
  if (name_length != 0) {
    for (std::size_t i = 0; i < name_length; ++i) emit_crc_ = crc32_update(emit_crc_, name[i]);
    if (!output_.write || !output_.write(output_.context, name, name_length))
      ++counters_.output_failures;
  }
}

void MaintenanceSession::send_bytes(const std::uint8_t* data, std::size_t length) noexcept {
  for (std::size_t i = 0; i < length; ++i) emit_crc_ = crc32_update(emit_crc_, data[i]);
  if (length != 0 && (!output_.write || !output_.write(output_.context, data, length)))
    ++counters_.output_failures;
}

void MaintenanceSession::send_end() noexcept {
  const std::uint32_t crc = crc32_final(emit_crc_);
  const std::uint8_t trailer[kCrcBytes] = {
      static_cast<std::uint8_t>(crc), static_cast<std::uint8_t>(crc >> 8),
      static_cast<std::uint8_t>(crc >> 16), static_cast<std::uint8_t>(crc >> 24)};
  if (!output_.write || !output_.write(output_.context, trailer, sizeof(trailer)))
    ++counters_.output_failures;
}

}  // namespace maintenance
}  // namespace orbit
