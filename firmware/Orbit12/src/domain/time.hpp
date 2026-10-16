#pragma once

#include <cstdint>

namespace orbit {

// Deadlines must be less than 2^31 units ahead. This is the standard modular
// ordering used by the source tick helper and remains valid across uint32 wrap.
constexpr bool deadline_reached(std::uint32_t now, std::uint32_t deadline) noexcept {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

constexpr std::uint32_t elapsed(std::uint32_t now, std::uint32_t then) noexcept {
  return now - then;
}

constexpr bool valid_deadline_distance(std::uint32_t now, std::uint32_t deadline) noexcept {
  return (deadline - now) <= 0x7fffffffU;
}

constexpr std::uint32_t add_deadline(std::uint32_t now, std::uint32_t delay) noexcept {
  return now + delay;
}

static_assert(deadline_reached(10U, 10U));
static_assert(deadline_reached(0x00000002U, 0xfffffffeU));
static_assert(!deadline_reached(0xfffffffeU, 0x00000002U));
static_assert(elapsed(2U, 0xfffffffeU) == 4U);

}  // namespace orbit
