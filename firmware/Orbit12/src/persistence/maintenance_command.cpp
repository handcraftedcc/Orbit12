#include "maintenance_command.hpp"

#include <cstring>

namespace orbit {
namespace maintenance {
namespace {

bool is_run_command(std::uint8_t type) noexcept {
  return type == static_cast<std::uint8_t>(FrameType::Info) ||
         type == static_cast<std::uint8_t>(FrameType::Enter) ||
         type == static_cast<std::uint8_t>(FrameType::Exit);
}

bool is_data_request(std::uint8_t type) noexcept {
  return type == static_cast<std::uint8_t>(FrameType::List) ||
         type == static_cast<std::uint8_t>(FrameType::Get) ||
         type == static_cast<std::uint8_t>(FrameType::Put) ||
         type == static_cast<std::uint8_t>(FrameType::Abort) ||
         type == static_cast<std::uint8_t>(FrameType::Format);
}

bool name_is(const std::uint8_t* name, std::size_t length, const char* token,
             std::size_t token_length) noexcept {
  return length == token_length && std::memcmp(name, token, token_length) == 0;
}

}  // namespace

void RunCommandFramer::feed(std::uint8_t byte, std::uint32_t now_ms, const RunSink& sink) noexcept {
  switch (state_) {
    case State::Hunt:
      hunt(byte, now_ms, sink);
      return;
    case State::Header:
      last_ms_ = now_ms;
      raw_[raw_count_++] = byte;
      crc_ = crc32_update(crc_, byte);
      header_[header_count_++] = byte;
      if (header_count_ < sizeof(header_)) return;
      {
        const std::uint8_t type = header_[0];
        const std::uint8_t name_length = header_[1];
        const std::uint64_t payload = static_cast<std::uint32_t>(header_[2]) |
                                      (static_cast<std::uint32_t>(header_[3]) << 8) |
                                      (static_cast<std::uint32_t>(header_[4]) << 16) |
                                      (static_cast<std::uint32_t>(header_[5]) << 24);
        payload_length_ = static_cast<std::uint32_t>(payload);
        command_.type = type;
        command_.name_length = 0;
        if (name_length > kNameBytes || (!is_run_command(type) && !is_data_request(type))) {
          // A framing error has no trustworthy length: report it and scan again from here.
          ++counters_.bad_frames;
          deliver(NakReason::BadFrame, sink);
          to_hunt();
          return;
        }
        const std::uint64_t declared = static_cast<std::uint64_t>(name_length) + payload + kCrcBytes;
        if (is_data_request(type)) {
          // Refused before any byte is buffered; the bytes are discarded, not scanned.
          ++counters_.refused;
          deliver(NakReason::NotInMaintenance, sink);
          discard_or_hunt(declared, now_ms);
          return;
        }
        if (payload_length_ != 0) {
          ++counters_.bad_frames;
          deliver(NakReason::BadFrame, sink);
          discard_or_hunt(declared, now_ms);
          return;
        }
        command_.name_length = name_length;
        name_count_ = 0;
        trailer_count_ = 0;
        state_ = name_length == 0 ? State::Trailer : State::Name;
      }
      return;
    case State::Name:
      last_ms_ = now_ms;
      raw_[raw_count_++] = byte;
      crc_ = crc32_update(crc_, byte);
      command_.name[name_count_++] = byte;
      if (name_count_ == command_.name_length) {
        trailer_count_ = 0;
        state_ = State::Trailer;
      }
      return;
    case State::Trailer:
      last_ms_ = now_ms;
      raw_[raw_count_++] = byte;
      trailer_[trailer_count_++] = byte;
      if (trailer_count_ == kCrcBytes) finish_trailer(sink);
      return;
    case State::Discard:
      last_ms_ = now_ms;
      if (--remaining_ == 0) to_hunt();
      return;
  }
}

void RunCommandFramer::poll(std::uint32_t now_ms, const RunSink& sink) noexcept {
  if (state_ == State::Hunt && !sync0_) return;
  if (static_cast<std::uint32_t>(now_ms - last_ms_) < kGapMs) return;
  if (state_ == State::Discard) {
    ++counters_.discard_drops;   // payload bytes are data: dropped, never scanned
    to_hunt();
    return;
  }
  if (state_ == State::Hunt) {
    to_hunt();                   // a lone 0x4F that began no frame
    return;
  }
  ++counters_.gap_resyncs;
  replay(now_ms, sink);
}

void RunCommandFramer::hunt(std::uint8_t byte, std::uint32_t now_ms, const RunSink& sink) noexcept {
  if (!sync0_) {
    if (byte == kSync0) {
      sync0_ = true;
      raw_[0] = byte;
      raw_count_ = 1;
      last_ms_ = now_ms;
    } else if (sink.passthrough != nullptr) {
      sink.passthrough(sink.context, byte);
    }
    return;
  }
  if (byte == kSync1) {
    sync0_ = false;
    raw_[1] = byte;
    raw_count_ = 2;
    crc_ = crc32_update(crc32_update(crc32_init(), kSync0), kSync1);
    header_count_ = 0;
    name_count_ = 0;
    trailer_count_ = 0;
    last_ms_ = now_ms;
    state_ = State::Header;
    return;
  }
  // The pending 0x4F began no frame: it passes through, and this byte is scanned again.
  sync0_ = false;
  raw_count_ = 0;
  if (sink.passthrough != nullptr) sink.passthrough(sink.context, kSync0);
  hunt(byte, now_ms, sink);
}

void RunCommandFramer::deliver(NakReason verdict, const RunSink& sink) noexcept {
  command_.verdict = verdict;
  ++counters_.commands;
  if (sink.command != nullptr) sink.command(sink.context, command_);
}

void RunCommandFramer::discard_or_hunt(std::uint64_t declared, std::uint32_t now_ms) noexcept {
  if (declared <= kMaxDiscardBytes) {
    remaining_ = static_cast<std::uint32_t>(declared);
    last_ms_ = now_ms;
    state_ = State::Discard;
    return;
  }
  to_hunt();
}

void RunCommandFramer::finish_trailer(const RunSink& sink) noexcept {
  const std::uint32_t expected = crc32_final(crc_);
  const std::uint32_t received = static_cast<std::uint32_t>(trailer_[0]) |
                                 (static_cast<std::uint32_t>(trailer_[1]) << 8) |
                                 (static_cast<std::uint32_t>(trailer_[2]) << 16) |
                                 (static_cast<std::uint32_t>(trailer_[3]) << 24);
  if (received != expected) {
    ++counters_.bad_checksum;
    deliver(NakReason::BadChecksum, sink);
  } else if (command_.type == static_cast<std::uint8_t>(FrameType::Info)) {
    if (command_.name_length != 0) {
      ++counters_.bad_frames;
      deliver(NakReason::BadFrame, sink);
    } else {
      deliver(NakReason::None, sink);
    }
  } else if (command_.type == static_cast<std::uint8_t>(FrameType::Enter)) {
    if (name_is(command_.name, command_.name_length, kEnterToken, kEnterTokenLength)) {
      deliver(NakReason::None, sink);
    } else {
      ++counters_.bad_token;
      deliver(NakReason::BadToken, sink);
    }
  } else {
    if (name_is(command_.name, command_.name_length, kExitToken, kExitTokenLength)) {
      deliver(NakReason::None, sink);
    } else {
      ++counters_.bad_token;
      deliver(NakReason::BadToken, sink);
    }
  }
  to_hunt();
}

void RunCommandFramer::to_hunt() noexcept {
  state_ = State::Hunt;
  sync0_ = false;
  raw_count_ = 0;
  header_count_ = 0;
  name_count_ = 0;
  trailer_count_ = 0;
  remaining_ = 0;
}

// Bytes after the sync of an abandoned partial frame are scanned again, so a '?' or a
// complete frame inside them is not lost. The copy is bounded by kRawBytes.
void RunCommandFramer::replay(std::uint32_t now_ms, const RunSink& sink) noexcept {
  std::uint8_t copy[kRawBytes]{};
  std::uint8_t count = 0;
  for (std::uint8_t i = 2; i < raw_count_; ++i) copy[count++] = raw_[i];
  to_hunt();
  for (std::uint8_t i = 0; i < count; ++i) feed(copy[i], now_ms, sink);
}

}  // namespace maintenance
}  // namespace orbit
