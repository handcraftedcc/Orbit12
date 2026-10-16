// Source: circuitpython-source/src/apps/Orion/modules/delay.py and bounce.py,
// with core/note_array.py (pop_due, append, remove ordering), core/utils.py
// (random_int) and the adafruit_ticks helpers.
//
// Deliberate deviations (each covered by a test in tests/native/t09c):
//  1. No release is dropped. The source silently loses a note-off when its
//     12-entry release queue is full, leaving the note sounding. Here:
//     - Delay sends a release that finds the queue full at once, and cancels
//       the pending note-on it ends, if that note-on has not sounded yet.
//       Counted as release_immediate and pending_on_cancelled.
//     - Bounce skips a note or repeat that would need a release while the queue
//       is full. It emits nothing for it, so no orphan release is left.
//       Counted as release_full_skipped.
//  2. Same-call order. Flush sends note-offs before note-ons. A due release
//     whose own note-on is emitted in the same call waits for the next call.
//     The source would send the release first and leave the note sounding.
//     Matching is by note identity, so a different note-on for the same pitch
//     in that call does not delay the release.
//  3. Unrecorded note-offs. When the note-delay table is full, the source ends
//     the note with DLY alone. That can release the note before its own
//     note-on. Here the release is clamped to the latest pending note-on of its
//     pitch and carries that note's identity.
//  4. Negative total delay. The source raises OverflowError (array('I') rejects
//     the stored record) and stops. Here the note is due in the past and sounds
//     on its call. Its release goes out on that call too, unless the note-on was
//     emitted in the same call as the note-off (note 2), then on the next call.
//  5. Time base. Deadlines are 32-bit milliseconds (or MIDI ticks) compared with
//     signed modular differences, not adafruit_ticks' 2^29 wrap. Ordering is the
//     same for deltas below 2^31. Source tick arithmetic is unbounded, and its
//     array('I') storage fails at 2^32. Here ticks wrap.
//  6. Velocity. Bounce clamps repeat velocity to 1..127. The source can emit a
//     value above 127 when VELMOD is negative, which is not a valid MIDI byte.
//  7. Numeric range. Float-to-int conversions in bounce clamp to +-(2^31-1)
//     before truncation, so there is no undefined behaviour. Python's float
//     overflow in 0.01 ** x raises OverflowError; here the interval clamps.
//  8. Note-off velocity is 0, following the other processors. Channel is kept
//     from the input note. Matching stays pitch-only (FIFO), as in the source.
//  9. Refused note-ons and delay records are counted, not silent. Bounce skips
//     a note while the output is full, as the source does, and emits no orphan
//     release for it.
#include "delay_bounce.hpp"
#include "drop_randomize.hpp"
#include "../music.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

namespace orbit {
namespace {
constexpr std::uint8_t kNone = 0;
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();
constexpr float kFloatMin = -(std::numeric_limits<float>::max)();
constexpr float kFloatMax = (std::numeric_limits<float>::max)();
constexpr std::int64_t kClampMax = 2147483647LL;
constexpr const char* kDelayLabel = "DLY";
constexpr const char* kDelayRandomLabel = "DLRND";
constexpr const char* kBounceLabel = "BOUNCE";
constexpr const char* kModeLabel = "MODE";
constexpr const char* kIntervalMsLabel = "INTMS";
constexpr const char* kIntervalRateLabel = "INTRT";
constexpr const char* kGateLabel = "GATE";
constexpr const char* kIntervalModLabel = "INTMOD";
constexpr const char* kVelocityModLabel = "VELMOD";
constexpr const char* kGateModLabel = "GTEMOD";
constexpr const char* kCurveLabel = "MODCRV";
constexpr const char* kModeOptions[] = {"MS", "RATE"};
constexpr const char* kCurveOptions[] = {"LIN", "CUBIC", "RAND"};

const ParamSpec kDelayParams[] = {
    {"delay", ParamType::Integer, ParamValue::integer(100), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(5), kDelayLabel, nullptr, 0, ParamPersist},
    {"delay_random", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(5), kDelayRandomLabel, nullptr, 0, ParamPersist}};

const ParamSpec kBounceParams[] = {
    {"bounces", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(0), ParamValue::integer(64), ParamValue::integer(1), kBounceLabel, nullptr, 0, ParamPersist},
    {"interval_mode", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), kModeLabel, kModeOptions, 2, ParamPersist},
    {"interval_ms", ParamType::Integer, ParamValue::integer(150), ParamValue::integer(kIntMin), ParamValue::integer(kIntMax), ParamValue::integer(5), kIntervalMsLabel, nullptr, 0, ParamPersist},
    {"interval_rate", ParamType::Integer, ParamValue::integer(8), ParamValue::integer(0), ParamValue::integer(12), ParamValue::integer(1), kIntervalRateLabel, nullptr, 0, ParamPersist},
    {"gate", ParamType::Decimal, ParamValue::decimal(100.0f), ParamValue::decimal(kFloatMin), ParamValue::decimal(kFloatMax), ParamValue::decimal(5.0f), kGateLabel, nullptr, 0, ParamPersist},
    {"interval_mod", ParamType::Decimal, ParamValue::decimal(0.0f), ParamValue::decimal(kFloatMin), ParamValue::decimal(kFloatMax), ParamValue::decimal(0.05f), kIntervalModLabel, nullptr, 0, ParamPersist},
    {"velocity_mod", ParamType::Decimal, ParamValue::decimal(1.0f), ParamValue::decimal(kFloatMin), ParamValue::decimal(kFloatMax), ParamValue::decimal(0.05f), kVelocityModLabel, nullptr, 0, ParamPersist},
    {"gate_mod", ParamType::Decimal, ParamValue::decimal(0.0f), ParamValue::decimal(kFloatMin), ParamValue::decimal(kFloatMax), ParamValue::decimal(0.05f), kGateModLabel, nullptr, 0, ParamPersist},
    {"mod_curve", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(2), ParamValue::integer(1), kCurveLabel, kCurveOptions, 3, ParamPersist}};

void descriptor_destroy(ModuleState&) noexcept {}
void attr_none(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kDelayDescriptor{"delay", 1, kDelayParams, 2, construct_delay_state, descriptor_destroy, process_delay, reset_delay_state, set_delay_param, attr_none};
const ModuleDescriptor kBounceDescriptor{"bounce", 1, kBounceParams, 9, construct_bounce_state, descriptor_destroy, process_bounce, reset_bounce_state, set_bounce_param, attr_none};

// Signed modular comparison: true when a is strictly before b (ticks_less).
bool before(std::uint32_t a, std::uint32_t b) noexcept { return static_cast<std::int32_t>(a - b) < 0; }

std::uint32_t advance(std::uint32_t base, std::int64_t delta) noexcept {
  return base + static_cast<std::uint32_t>(delta);
}

// Python int(): truncate toward zero. Out-of-range values clamp (note 7).
std::int64_t truncate_clamped(double value) noexcept {
  constexpr double kLimit = 2147483647.0;
  if (value != value) return 0;
  if (value >= kLimit) return kClampMax;
  if (value <= -kLimit) return -kClampMax;
  return static_cast<std::int64_t>(value);
}

bool int_in(const ParamValue& value, std::int32_t low, std::int32_t high, std::int32_t& out) noexcept {
  if (value.type != ParamType::Integer && value.type != ParamType::Enumeration) return false;
  if (value.value.integer < low || value.value.integer > high) return false;
  out = value.value.integer;
  return true;
}

bool finite_in(const ParamValue& value, double& out) noexcept {
  if (value.type != ParamType::Decimal) return false;
  const float f = value.value.decimal;
  if (!std::isfinite(f)) return false;
  out = static_cast<double>(f);
  return true;
}

// Note identities are nonzero; zero means unknown.
std::uint32_t take_id(std::uint32_t& next) noexcept {
  const std::uint32_t id = next;
  next = next == 0xFFFFFFFFU ? 1U : next + 1U;
  return id;
}

bool contains_id(const std::uint32_t* ids, std::uint8_t count, std::uint32_t id) noexcept {
  if (id == 0) return false;
  for (std::uint8_t i = 0; i < count; ++i) {
    if (ids[i] == id) return true;
  }
  return false;
}

// Emits due releases in queue order while the output has room. A release whose
// own note-on was emitted in this call (popped) waits for the next call. Anything
// not emitted stays queued.
void drain_releases(ScheduledOffs& offs, std::uint32_t now, const std::uint32_t* popped, std::uint8_t popped_count,
                    NoteBuffer& out) noexcept {
  std::uint8_t kept = 0;
  for (std::uint8_t i = 0; i < offs.count; ++i) {
    const ScheduledOff item = offs.items[i];
    if (before(item.due, now) && !contains_id(popped, popped_count, item.on_id) &&
        out.append({item.pitch, 0, item.channel}) == PushResult::Added) {
      continue;
    }
    offs.items[kept++] = item;
  }
  offs.count = kept;
}

void pop_due_ons(DelayModuleState& s, std::uint32_t now, NoteBuffer& out, std::uint32_t* popped,
                 std::uint8_t& popped_count) noexcept {
  std::uint8_t kept = 0;
  for (std::uint8_t i = 0; i < s.on_count; ++i) {
    const TimedNote item = s.ons[i];
    if (before(item.due, now) && out.append(item.note) == PushResult::Added) {
      popped[popped_count++] = item.id;
      continue;
    }
    s.ons[kept++] = item;
  }
  s.on_count = kept;
}

void cancel_pending_on(DelayModuleState& s, std::uint32_t id) noexcept {
  for (std::uint8_t k = 0; k < s.on_count; ++k) {
    if (s.ons[k].id != id) continue;
    for (std::uint8_t j = static_cast<std::uint8_t>(k + 1); j < s.on_count; ++j) s.ons[j - 1] = s.ons[j];
    --s.on_count;
    ++s.counters.pending_on_cancelled;
    return;
  }
}

// Queues a delay release. A full queue sends it now instead. The pending
// note-on it ends is cancelled first, so the note never sounds after its
// release. The caller has emitted no releases yet, and at most 12 releases
// arrive per call, so the output has room.
void release_delay_note(DelayModuleState& s, std::uint8_t pitch, std::uint8_t channel, std::uint32_t due,
                        std::uint32_t on_id, NoteBuffer& out) noexcept {
  if (s.offs.count < kPolyphony) {
    s.offs.items[s.offs.count++] = ScheduledOff{pitch, channel, due, on_id};
    return;
  }
  if (on_id != 0) cancel_pending_on(s, on_id);
  (void)out.append({pitch, 0, channel});
  ++s.counters.release_immediate;
}

void remove_note_delay(DelayModuleState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = static_cast<std::uint8_t>(index + 1); i < s.note_delay_count; ++i) s.note_delays[i - 1] = s.note_delays[i];
  --s.note_delay_count;
}

void remove_repeat(BounceModuleState& s, std::uint8_t index) noexcept {
  for (std::uint8_t i = static_cast<std::uint8_t>(index + 1); i < s.repeat_count; ++i) s.repeats[i - 1] = s.repeats[i];
  --s.repeat_count;
}

bool is_release_reset(ResetReason reason) noexcept {
  return reason == ResetReason::Stop || reason == ResetReason::SceneChange ||
         reason == ResetReason::Panic || reason == ResetReason::Init;
}
}  // namespace

void construct_delay_state(ModuleState& state) noexcept { new (&state.storage.delay) DelayModuleState{}; }
void construct_bounce_state(ModuleState& state) noexcept { new (&state.storage.bounce) BounceModuleState{}; }

// Source Delay.process. Each note-on is scheduled at DLY + random(DLRND, seed
// tick+i) and its delay is recorded per pitch. Each note-off uses the oldest
// recorded delay for its pitch, or DLY when none matches. Source order is kept:
// note-ons, note-offs, due note-ons, due releases.
void process_delay(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.delay;
  output = {};
  const std::uint32_t now = context.now.ms;
  const std::uint64_t tick = context.transport.tick;

  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    const Note& note = input.on.items[i];
    const std::uint32_t id = take_id(s.next_id);
    const std::int64_t random = seeded::random_int(tick + i, s.delay_random, 0);
    const std::int64_t note_delay = static_cast<std::int64_t>(s.delay) + random;
    if (s.on_count < kPolyphony) {
      s.ons[s.on_count++] = TimedNote{note, advance(now, note_delay), id};
    } else {
      ++s.counters.on_overflow;
    }
    if (s.note_delay_count < kPolyphony) {
      s.note_delays[s.note_delay_count++] = DelayNoteTime{note.pitch, note_delay, id};
    } else {
      ++s.counters.delay_unrecorded;
    }
  }

  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    const Note& note = input.off.items[i];
    std::int64_t note_delay = s.delay;
    std::uint32_t on_id = 0;
    bool matched = false;
    for (std::uint8_t n = 0; n < s.note_delay_count; ++n) {
      if (s.note_delays[n].pitch != note.pitch) continue;
      note_delay = s.note_delays[n].delay;
      on_id = s.note_delays[n].id;
      matched = true;
      remove_note_delay(s, n);
      break;
    }
    std::uint32_t due = advance(now, note_delay);
    if (!matched) {
      // Note 3: end no earlier than the latest pending note-on of this pitch.
      bool found = false;
      std::uint32_t latest_due = 0;
      std::uint32_t latest_id = 0;
      for (std::uint8_t k = 0; k < s.on_count; ++k) {
        if (s.ons[k].note.pitch != note.pitch) continue;
        if (!found || !before(s.ons[k].due, latest_due)) {
          found = true;
          latest_due = s.ons[k].due;
          latest_id = s.ons[k].id;
        }
      }
      if (found) {
        if (before(due, latest_due)) due = latest_due;
        on_id = latest_id;
      }
    }
    release_delay_note(s, note.pitch, note.channel, due, on_id, output.off);
  }

  std::uint32_t popped[kPolyphony]{};
  std::uint8_t popped_count = 0;
  pop_due_ons(s, now, output.on, popped, popped_count);
  drain_releases(s.offs, now, popped, popped_count, output.off);
}

// Source Bounce.process. Input note-offs are ignored, as in the source; each
// note-on is released by its gate timer. Repeats are scheduled by MS or RATE.
void process_bounce(ModuleState& state, const ProcessContext& context, const NoteBatch& input, NoteBatch& output) noexcept {
  auto& s = state.storage.bounce;
  output = {};
  const std::uint32_t now = context.now.ms;
  const std::uint32_t cur_tick = context.transport.tick;
  const bool rate_mode = s.interval_mode == 1;

  // Releases due now go out before any note-on of this call. Nothing is popped.
  drain_releases(s.offs, now, nullptr, 0, output.off);

  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    if (s.repeat_count >= kPolyphony) {
      if (output.off.count >= kPolyphony) {
        ++s.counters.repeat_skipped;
        continue;
      }
      const Note evicted = s.repeats[0].note.note;
      (void)output.off.append({evicted.pitch, 0, evicted.channel});
      remove_repeat(s, 0);
    }

    const Note& note = input.on.items[i];
    // The source keeps scheduling repeats when its note-on is refused. Repeats
    // follow the same rule here, minus the orphan release that the source leaves.
    if (output.on.count >= kPolyphony) {
      ++s.counters.output_full;
    } else if (s.offs.count >= kPolyphony) {
      ++s.counters.release_full_skipped;  // Note 1: no release slot, so no note.
    } else {
      (void)output.on.append(note);
      const std::int64_t gate = truncate_clamped(s.gate);
      s.offs.items[s.offs.count++] = ScheduledOff{note.pitch, note.channel, advance(now, gate), 0};
    }

    if (s.bounces > 0) {
      const std::uint32_t due = rate_mode
                                    ? cur_tick + static_cast<std::uint32_t>(s.interval_rate)
                                    : advance(now, static_cast<std::int64_t>(s.interval_ms));
      if (s.repeat_count < kPolyphony) {
        s.repeats[s.repeat_count++] = BounceRepeat{TimedNote{note, due, 0}, static_cast<std::uint8_t>(s.bounces)};
      }
    }
  }

  std::uint8_t i = 0;
  while (i < s.repeat_count) {
    BounceRepeat& repeat = s.repeats[i];
    const bool due = rate_mode
                         ? static_cast<std::int32_t>(repeat.note.due - cur_tick) <= 0
                         : before(repeat.note.due, now);
    if (!due) {
      ++i;
      continue;
    }

    const Note note = repeat.note.note;
    std::int64_t remaining = repeat.remaining;

    double progress = static_cast<double>(s.bounces - remaining + 1) / static_cast<double>(s.bounces + 1);
    if (progress < 0) {
      progress = 0;
    } else if (progress > 1) {
      progress = 1;
    }
    if (s.mod_curve == 1) {
      progress = progress * progress * progress;
    } else if (s.mod_curve == 2) {
      const std::uint64_t seed = static_cast<std::uint64_t>(cur_tick) + note.pitch + static_cast<std::uint64_t>(remaining);
      progress = static_cast<double>(seeded::random_int(seed, 100, 0)) / 100.0;
    }

    std::int64_t velocity = truncate_clamped(static_cast<double>(note.velocity) * (1.0 - (s.velocity_mod * progress)));
    std::int64_t gate = truncate_clamped(s.gate * (1.0 - (s.gate_mod * progress)));
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    if (gate < 1) gate = 1;

    if (output.on.count >= kPolyphony) {
      ++s.counters.output_full;
    } else if (s.offs.count >= kPolyphony) {
      ++s.counters.release_full_skipped;  // Note 1: no release slot, so no repeat.
    } else {
      (void)output.on.append({note.pitch, static_cast<std::uint8_t>(velocity), note.channel});
      s.offs.items[s.offs.count++] = ScheduledOff{note.pitch, note.channel, advance(now, gate), 0};
    }

    remaining -= 1;
    if (remaining <= 0) {
      remove_repeat(s, i);
      continue;
    }
    repeat.remaining = static_cast<std::uint8_t>(remaining);

    std::int64_t interval = 0;
    if (rate_mode) {
      const double scaled = static_cast<double>(s.interval_rate) * std::pow(0.01, s.interval_mod * progress);
      interval = truncate_clamped(scaled + 0.5);
    } else {
      interval = truncate_clamped(static_cast<double>(s.interval_ms) * std::pow(0.01, s.interval_mod * progress));
    }
    if (interval < 1) interval = 1;
    repeat.note.due = rate_mode ? cur_tick + static_cast<std::uint32_t>(interval) : advance(now, interval);
    ++i;
  }
}

// Stop, scene change, panic and init clear the schedules. Owed releases are
// dropped here; the controller sends all-notes-off at these transitions.
void reset_delay_state(ModuleState& state, ResetReason reason) noexcept {
  if (!is_release_reset(reason)) return;
  auto& s = state.storage.delay;
  s.on_count = 0;
  s.note_delay_count = 0;
  s.offs.count = 0;
}

void reset_bounce_state(ModuleState& state, ResetReason reason) noexcept {
  if (!is_release_reset(reason)) return;
  auto& s = state.storage.bounce;
  s.repeat_count = 0;
  s.offs.count = 0;
}

bool set_delay_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.delay;
  std::int32_t v = 0;
  switch (index) {
    case 0: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.delay = v; return true;
    case 1: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.delay_random = v; return true;
    default: return false;
  }
}

bool set_bounce_param(ModuleState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = kNone;
  auto& s = state.storage.bounce;
  std::int32_t v = 0;
  double d = 0.0;
  switch (index) {
    case 0: if (!int_in(value, 0, 64, v)) return false; s.bounces = v; return true;
    case 1: if (!int_in(value, 0, 1, v)) return false; s.interval_mode = static_cast<std::uint8_t>(v); return true;
    case 2: if (!int_in(value, kIntMin, kIntMax, v)) return false; s.interval_ms = v; return true;
    case 3:
      if (!int_in(value, 0, static_cast<std::int32_t>(music::kRateMidiTicks.size()) - 1, v)) return false;
      s.interval_rate_value = static_cast<std::uint8_t>(v);
      s.interval_rate = music::kRateMidiTicks[static_cast<std::size_t>(v)];
      return true;
    case 4: if (!finite_in(value, d)) return false; s.gate = d; return true;
    case 5: if (!finite_in(value, d)) return false; s.interval_mod = d; return true;
    case 6: if (!finite_in(value, d)) return false; s.velocity_mod = d; return true;
    case 7: if (!finite_in(value, d)) return false; s.gate_mod = d; return true;
    case 8: if (!int_in(value, 0, 2, v)) return false; s.mod_curve = static_cast<std::uint8_t>(v); return true;
    default: return false;
  }
}

const ModuleDescriptor& delay_module_descriptor() noexcept { return kDelayDescriptor; }
const ModuleDescriptor& bounce_module_descriptor() noexcept { return kBounceDescriptor; }
}  // namespace orbit
