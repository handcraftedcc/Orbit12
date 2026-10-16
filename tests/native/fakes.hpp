#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "domain/types.hpp"

// Small deterministic port fakes shared by native tests. MIDI/storage methods
// are refusal-shaped placeholders until their public port signatures exist;
// they do not claim conformance to a production MidiPort or StoragePort API.
struct FakeClock {
  static constexpr std::size_t kCapacity = 16;
  std::array<orbit::TimeSample, kCapacity> samples{};
  std::size_t count = 0;
  std::size_t cursor = 0;

  orbit::TimeSample sample() noexcept {
    if (count == 0) return {};
    const auto index = cursor < count ? cursor : count - 1;
    ++cursor;
    return samples[index];
  }

  bool push(orbit::TimeSample value) noexcept {
    if (count == samples.size()) return false;
    samples[count++] = value;
    return true;
  }
};

struct FakeRandom {
  static constexpr std::size_t kCapacity = 32;
  std::array<std::uint32_t, kCapacity> values{};
  std::size_t count = 0;
  std::size_t cursor = 0;

  static std::uint32_t next(void* context) noexcept {
    auto& self = *static_cast<FakeRandom*>(context);
    if (self.count == 0) return 0;
    const auto index = self.cursor < self.count ? self.cursor : self.count - 1;
    ++self.cursor;
    return self.values[index];
  }

  orbit::RandomSource source() noexcept { return {this, &FakeRandom::next}; }

  bool push(std::uint32_t value) noexcept {
    if (count == values.size()) return false;
    values[count++] = value;
    return true;
  }
};

struct RefusingMidi {
  std::uint32_t attempts = 0;

  template <typename Packet>
  bool try_send(const Packet&) noexcept {
    ++attempts;
    return false;
  }
};

struct RefusingStorage {
  std::uint32_t calls = 0;

  bool read(const char*, std::uint8_t*, std::size_t) noexcept {
    ++calls;
    return false;
  }
  bool write(const char*, const std::uint8_t*, std::size_t) noexcept {
    ++calls;
    return false;
  }
  bool rename(const char*, const char*) noexcept {
    ++calls;
    return false;
  }

  bool close() noexcept {
    ++calls;
    return false;
  }
};
