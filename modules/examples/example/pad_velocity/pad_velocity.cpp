// Example user input. Each logical pad sounds MIDI 60 + pad, shifted by transpose and clamped to 0..127.
// Logical pads come from the physical keys through music::kPadMap, as the core inputs do.
// Velocity is pad_vel[pad] * velocity / 127 (at least 1, so a note-on never becomes a note-off).
// Every sounding pad is tracked, so a note-off releases exactly the pitch its note-on sounded.
#include "pad_velocity.hpp"

#include <new>

#include "../../../../firmware/Orbit12/src/domain/music.hpp"

namespace orbit {
namespace {
constexpr int kBaseNote = 60;
constexpr RgbColor kPadColor{0, 126, 129};

const ParamSpec kParams[] = {
  {"velocity", ParamType::Integer, ParamValue::integer(100), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist},
  {"transpose", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(-24), ParamValue::integer(24), ParamValue::integer(1), "TRANS", nullptr, 0, ParamPersist}};

PadVelocityInputState& storage(InputState& state) noexcept { return state.storage.user_example_pad_velocity; }

// Physical key (0..11) to logical pad, the inverse of music::kPadMap.
std::uint8_t pad_index(std::uint8_t key) noexcept {
  for (std::size_t pad = 0; pad < music::kPadMap.size(); ++pad) {
    if (music::kPadMap[pad] == key) return static_cast<std::uint8_t>(pad);
  }
  return 0;
}

void construct(InputState& state) noexcept { new (&state.storage.user_example_pad_velocity) PadVelocityInputState{}; }
void destroy(InputState&) noexcept {}
void reset(InputState& state, ResetReason reason) noexcept {
  // Core convention: a stop, scene change, panic or init forgets the held pads (the controller releases sounding notes).
  if (reason == ResetReason::Stop || reason == ResetReason::SceneChange || reason == ResetReason::Panic || reason == ResetReason::Init) {
    auto& s = storage(state);
    for (std::uint8_t pad = 0; pad < PadVelocityInputState::kPadCount; ++pad) s.held[pad] = false;
  }
}

std::uint8_t clamp_midi(int value) noexcept {
  if (value < 0) return 0;
  if (value > 127) return 127;
  return static_cast<std::uint8_t>(value);
}

void process(InputState& state, const ProcessContext&, const InputFrame& frame, NoteBatch& out, InputView& view) noexcept {
  auto& s = storage(state);
  out = {};
  for (auto& color : view.pad_colors) color = kPadColor;
  for (std::uint16_t i = 0; i < frame.keys.count; ++i) {
    const KeyEvent& event = frame.keys.items[i];
    if (!event.valid()) continue;
    const std::uint8_t pad = pad_index(event.key);
    if (event.down) {
      // A re-press first releases the pitch the pad still holds.
      if (s.held[pad]) {
        if (out.off.append({s.held_pitch[pad], 0, 0}) != PushResult::Added) continue;
        s.held[pad] = false;
      }
      const std::uint8_t pitch = clamp_midi(kBaseNote + pad + s.transpose);
      int velocity = static_cast<int>(s.pad_vel[pad]) * static_cast<int>(s.velocity) / 127;
      if (velocity < 1) velocity = 1;
      if (velocity > 127) velocity = 127;
      if (out.on.append({pitch, static_cast<std::uint8_t>(velocity), 0}) == PushResult::Added) {
        s.held_pitch[pad] = pitch;
        s.held[pad] = true;
      }  // else the batch overflowed: the chain faults and releases all notes
    } else if (s.held[pad]) {
      if (out.off.append({s.held_pitch[pad], 0, 0}) == PushResult::Added) s.held[pad] = false;
    }
  }
}

bool set_param(InputState& state, std::uint8_t index, ParamValue value, const GlobalState&, std::uint8_t& effects) noexcept {
  effects = EffectNone;
  auto& s = storage(state);
  if (index == 0 && value.type == ParamType::Integer && value.value.integer >= 0 && value.value.integer <= 127) {
    s.velocity = static_cast<std::uint8_t>(value.value.integer);
    return true;
  }
  if (index == 1 && value.type == ParamType::Integer && value.value.integer >= -24 && value.value.integer <= 24) {
    s.transpose = value.value.integer;  // held pads keep their sounded pitch until released
    return true;
  }
  return false;
}

void attributes(InputState& state, AttributeVisitor& visitor) noexcept {
  if (!visitor.visit) return;
  auto& s = storage(state);
  AttributeDescriptor attr{"pad_vel", AttributeType::Byte, PadVelocityInputState::kPadCount, s.pad_vel};
  (void)visitor.visit(visitor.ctx, attr);
}

const InputDescriptor kDescriptor{"user.example.pad_velocity", 1, kParams, 2, construct, destroy, process, reset, set_param, attributes};
}  // namespace

const InputDescriptor& user_example_pad_velocity_input_descriptor() noexcept { return kDescriptor; }
}  // namespace orbit
