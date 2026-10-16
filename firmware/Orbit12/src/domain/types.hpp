#pragma once

#include <cstddef>
#include <cstdint>

namespace orbit {

constexpr std::size_t kKeys = 12;
constexpr std::size_t kProcessors = 6;
constexpr std::size_t kPolyphony = 12;
constexpr std::size_t kOutputCapacity = 24;
constexpr std::size_t kInputEventCapacity = 64;
constexpr std::size_t kSceneCount = 10;
constexpr std::size_t kScaleCount = 21;
constexpr std::uint8_t kMidiMax = 127;
constexpr std::uint8_t kMidiChannelMax = 15;

struct TimeSample {
  std::uint32_t ms = 0;
  std::uint32_t us = 0;
};

struct Note {
  std::uint8_t pitch = 0;
  std::uint8_t velocity = 0;
  std::uint8_t channel = 0;  // Domain channels are zero-based.

  constexpr bool valid() const noexcept {
    return pitch <= kMidiMax && velocity <= kMidiMax && channel <= kMidiChannelMax;
  }
};

enum class PushResult : std::uint8_t { Added, Duplicate, Full, Invalid };

// Append preserves multiplicity and order. Unique insertion leaves the buffer
// unchanged when an equal item already exists; identity is chosen by the caller.
template <typename T, std::size_t N>
struct FixedBuffer {
  static_assert(N > 0 && N <= UINT16_MAX, "FixedBuffer capacity must fit count");

  T items[N]{};
  std::uint16_t count = 0;
  // Sticky: set by any append that found the buffer full (an item was refused),
  // cleared only by clear()/reassignment. Lets the chain detect a lost note-off
  // that a module ignored, without per-module cooperation.
  bool overflowed = false;

  PushResult append(const T& value) noexcept {
    if (count >= N) { overflowed = true; return PushResult::Full; }
    items[count++] = value;
    return PushResult::Added;
  }

  template <typename Equal>
  PushResult insert_unique(const T& value, Equal equal) noexcept {
    for (std::uint16_t i = 0; i < count; ++i) {
      if (equal(items[i], value)) return PushResult::Duplicate;
    }
    return append(value);
  }

  void clear() noexcept { count = 0; overflowed = false; }
  constexpr std::size_t capacity() const noexcept { return N; }
  bool empty() const noexcept { return count == 0; }
};

using NoteBuffer = FixedBuffer<Note, kPolyphony>;

inline PushResult append_note(NoteBuffer& buffer, const Note& note) noexcept {
  return note.valid() ? buffer.append(note) : PushResult::Invalid;
}

template <typename Equal>
inline PushResult insert_unique_note(NoteBuffer& buffer, const Note& note, Equal equal) noexcept {
  return note.valid() ? buffer.insert_unique(note, equal) : PushResult::Invalid;
}

struct NoteBatch {
  NoteBuffer off;
  NoteBuffer on;
  bool overflowed() const noexcept { return off.overflowed || on.overflowed; }
};

struct KeyEvent {
  std::uint8_t key = 0;
  bool down = false;
  std::uint32_t at_ms = 0;

  constexpr bool valid() const noexcept { return key < kKeys; }
};

struct InputFrame {
  FixedBuffer<KeyEvent, kInputEventCapacity> keys;
  std::int16_t encoder_delta = 0;
  bool encoder_down = false;
  bool encoder_pressed = false;
  bool encoder_released = false;
};

enum class TransportMode : std::uint8_t { Internal = 0, External = 1 };
enum class TimingStep : std::uint8_t { Sixteenth = 0, ThirtySecond = 1, SixtyFourth = 2 };

// Values correspond to State.reset_to_defaults and SET parameter callbacks.
// Key and scale indices follow the source's NOTES and SCALENAMES tables.
struct GlobalState {
  std::uint8_t key = 0;       // C
  std::uint8_t scale = 1;     // Major
  // Python stores these as unrestricted ints; int32_t is the domain
  // representation. Persistence must reject values outside this range.
  std::int32_t octave = 3;
  std::int32_t key_offset = 0;
  std::uint16_t bpm = 120;
  float swing = 0.0f;
  TransportMode transport_mode = TransportMode::Internal;
  TimingStep timing_step = TimingStep::Sixteenth;

  bool valid() const noexcept {
    return key < kKeys && scale < kScaleCount && bpm >= 1 && bpm <= 300 &&
           swing >= 0.0f && swing <= 1.0f &&
           (transport_mode == TransportMode::Internal || transport_mode == TransportMode::External) &&
           static_cast<std::uint8_t>(timing_step) <= static_cast<std::uint8_t>(TimingStep::SixtyFourth);
  }
  bool set_key(std::uint8_t value) noexcept {
    if (value >= kKeys) return false;
    key = value;
    return true;
  }
  bool set_scale(std::uint8_t value) noexcept {
    if (value >= kScaleCount) return false;
    scale = value;
    return true;
  }
  bool set_bpm(std::uint16_t value) noexcept {
    if (value < 1 || value > 300) return false;
    bpm = value;
    return true;
  }
  bool set_swing(float value) noexcept {
    if (!(value >= 0.0f && value <= 1.0f)) return false;
    swing = value;
    return true;
  }
  bool set_transport_mode(std::uint8_t value) noexcept {
    if (value > 1) return false;
    transport_mode = static_cast<TransportMode>(value);
    return true;
  }
};

struct TransportView {
  bool running = false;
  bool ticked = false;
  std::uint32_t tick = 0;
  std::uint32_t next_us = 0;
};

struct RandomSource {
  void* ctx = nullptr;
  std::uint32_t (*next)(void*) = nullptr;
  bool draw(std::uint32_t& result) const noexcept {
    if (!next) return false;
    result = next(ctx);
    return true;
  }
};

struct ProcessContext {
  TimeSample now;
  const GlobalState& global;
  TransportView transport;
  RandomSource& random;
};

enum class Operation : std::uint8_t { Next, Add, OutNext, OutSource, Skip };
enum class ResetReason : std::uint8_t { Init, Stop, SceneChange, ParameterChange, Panic };

enum class ParamType : std::uint8_t { Integer, Decimal, Enumeration, Action, Boolean, Text };
enum ParamFlags : std::uint8_t {
  ParamPersist = 1U << 0,
  ParamUiOnly = 1U << 1,
};

struct ParamValue {
  ParamType type = ParamType::Integer;
  union Scalar {
    std::int32_t integer;
    float decimal;
    bool boolean;
    constexpr Scalar() : integer(0) {}
    constexpr explicit Scalar(std::int32_t v) : integer(v) {}
    constexpr explicit Scalar(float v) : decimal(v) {}
    constexpr explicit Scalar(bool v) : boolean(v) {}
  } value;

  constexpr ParamValue() noexcept = default;
  constexpr ParamValue(ParamType tag, Scalar scalar) noexcept : type(tag), value(scalar) {}

  static constexpr ParamValue integer(std::int32_t v) noexcept {
    return ParamValue(ParamType::Integer, Scalar(v));
  }
  static constexpr ParamValue decimal(float v) noexcept {
    return ParamValue(ParamType::Decimal, Scalar(v));
  }
  static constexpr ParamValue boolean(bool v) noexcept {
    return ParamValue(ParamType::Boolean, Scalar(v));
  }
};

enum class GlobalBinding : std::uint8_t { None, Octave, KeyOffset };

struct ParamSpec {
  const char* name = nullptr;
  ParamType type = ParamType::Integer;
  ParamValue default_value{};
  ParamValue minimum{};
  ParamValue maximum{};
  ParamValue step{};
  const char* display_label = nullptr;
  const char* const* options = nullptr;
  std::uint8_t option_count = 0;
  std::uint8_t flags = ParamPersist;
  // Controller applies these authoritative mutations after validating the
  // typed value and honoring release effects. Modules retain const snapshots.
  GlobalBinding global_binding = GlobalBinding::None;
};

enum EffectFlags : std::uint8_t {
  EffectNone = 0,
  EffectReleaseNotes = 1U << 0,
  EffectRepaint = 1U << 1,
  EffectResetModule = 1U << 2,
};

enum class AttributeType : std::uint8_t { Integer, Decimal, Boolean, Byte };
struct AttributeDescriptor {
  const char* name = nullptr;
  AttributeType type = AttributeType::Integer;
  std::uint16_t element_count = 1;
  // Address of a fixed scalar/array owned by the module state.
  void* values = nullptr;
};

struct AttributeVisitor {
  void* ctx = nullptr;
  bool (*visit)(void*, AttributeDescriptor&) = nullptr;
};

struct ModuleState;
struct InputState;

struct ModuleDescriptor {
  const char* id;
  std::uint16_t version;
  const ParamSpec* params;
  std::uint8_t param_count;
  void (*construct)(ModuleState&);
  void (*destroy)(ModuleState&);
  void (*process)(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&);
  void (*reset)(ModuleState&, ResetReason);
  bool (*set_param)(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects);
  void (*attributes)(ModuleState&, AttributeVisitor&);
};

struct RgbColor { std::uint8_t red = 0, green = 0, blue = 0; };
struct InputView {
  RgbColor pad_colors[kKeys]{};
  // Labels point at immutable module/static storage and stay valid until the
  // next input processing call; the array bounds the number of labels.
  const char* pad_labels[kKeys]{};
};

struct InputDescriptor {
  const char* id;
  std::uint16_t version;
  const ParamSpec* params;
  std::uint8_t param_count;
  void (*construct)(InputState&);
  void (*destroy)(InputState&);
  void (*process)(InputState&, const ProcessContext&, const InputFrame&, NoteBatch&, InputView&);
  void (*reset)(InputState&, ResetReason);
  bool (*set_param)(InputState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t& effects);
  void (*attributes)(InputState&, AttributeVisitor&);
};

static_assert(sizeof(Note) == 3, "Note must stay compact");
static_assert(alignof(Note) == 1, "Note uses byte alignment");
static_assert(alignof(FixedBuffer<Note, kPolyphony>) <= alignof(std::uint32_t));
static_assert(kOutputCapacity == 2 * kPolyphony, "Output capacity follows source note arrays");

}  // namespace orbit
