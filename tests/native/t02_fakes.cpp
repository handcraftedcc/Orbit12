#include <cstdint>
#include <cstdio>

#include "fakes.hpp"

namespace {

int failures = 0;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #expression);                                                \
      ++failures;                                                                \
    }                                                                            \
  } while (false)

void clock_is_bounded_and_repeatable() {
  FakeClock clock;
  CHECK(clock.sample().ms == 0U);  // Empty clock has a stable zero sample.
  for (std::size_t i = 0; i < FakeClock::kCapacity; ++i) {
    CHECK(clock.push(orbit::TimeSample{static_cast<std::uint32_t>(i),
                                       static_cast<std::uint32_t>(i * 1000U)}));
  }
  CHECK(!clock.push(orbit::TimeSample{99U, 99U}));
  for (std::size_t i = 0; i < FakeClock::kCapacity + 2; ++i) {
    const auto expected = static_cast<std::uint32_t>(
        i < FakeClock::kCapacity ? i : FakeClock::kCapacity - 1);
    const auto sample = clock.sample();
    CHECK(sample.ms == expected);
    CHECK(sample.us == expected * 1000U);
  }
}

void random_stream_is_bounded_and_repeatable() {
  FakeRandom random;
  const auto source = random.source();
  std::uint32_t value = 123U;
  CHECK(source.draw(value) && value == 0U);
  for (std::size_t i = 0; i < FakeRandom::kCapacity; ++i) {
    CHECK(random.push(static_cast<std::uint32_t>(i * 3U + 1U)));
  }
  CHECK(!random.push(999U));
  for (std::size_t i = 0; i < FakeRandom::kCapacity + 2; ++i) {
    const auto expectedIndex = i < FakeRandom::kCapacity ? i : FakeRandom::kCapacity - 1;
    CHECK(source.draw(value));
    CHECK(value == static_cast<std::uint32_t>(expectedIndex * 3U + 1U));
  }
}

void refusal_fakes_report_failures_without_claiming_success() {
  RefusingMidi midi;
  const std::uint8_t packet = 0x90U;
  CHECK(!midi.try_send(packet));
  CHECK(!midi.try_send(packet));
  CHECK(midi.attempts == 2U);

  RefusingStorage storage;
  std::uint8_t destination = 0x5aU;
  const std::uint8_t payload = 0x7fU;
  CHECK(!storage.read("Scene01.txt", &destination, 1U));
  CHECK(destination == 0x5aU);
  CHECK(!storage.write("Scene01.txt", &payload, 1U));
  CHECK(!storage.rename("Scene01.tmp", "Scene01.txt"));
  CHECK(!storage.close());
  CHECK(storage.calls == 4U);
}

}  // namespace

int main() {
  clock_is_bounded_and_repeatable();
  random_stream_is_bounded_and_repeatable();
  refusal_fakes_report_failures_without_claiming_success();
  if (failures != 0) {
    std::fprintf(stderr, "%d fake assertion(s) failed\n", failures);
    return 1;
  }
  std::puts("T02 deterministic fake checks passed");
  return 0;
}
