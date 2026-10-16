#include "inputs.hpp"
#include "../music.hpp"
#include <new>

namespace orbit {
namespace {
constexpr const char* kLayoutOptions[] = {"KEY", "CHRM"};
constexpr const char* kDrumOptions[] = {"ORD", "L12", "R12", "B3R", "B3F"};
constexpr std::int32_t kMin = (INT32_MIN);
constexpr std::int32_t kMax = (INT32_MAX);
constexpr std::uint8_t kReleaseRepaint = EffectReleaseNotes | EffectRepaint;
const ParamSpec kNoteParams[] = {
  {"octave", ParamType::Integer, ParamValue::integer(3), ParamValue::integer(kMin), ParamValue::integer(kMax), ParamValue::integer(1), "OCT", nullptr, 0, ParamPersist, GlobalBinding::Octave},
  {"key_offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kMin), ParamValue::integer(kMax), ParamValue::integer(1), "PADOFS", nullptr, 0, ParamPersist, GlobalBinding::KeyOffset},
  {"velocity", ParamType::Integer, ParamValue::integer(127), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist},
  {"layout", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(1), ParamValue::integer(1), "LAY", kLayoutOptions, 2, ParamPersist}};
const ParamSpec kDrumParams[] = {
  {"layout", ParamType::Enumeration, ParamValue::integer(0), ParamValue::integer(0), ParamValue::integer(4), ParamValue::integer(1), "LAY", kDrumOptions, 5, ParamPersist},
  {"base_note", ParamType::Integer, ParamValue::integer(36), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "BASE", nullptr, 0, ParamPersist},
  {"key_offset", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(kMin), ParamValue::integer(kMax), ParamValue::integer(1), "PADOFS", nullptr, 0, ParamPersist, GlobalBinding::KeyOffset},
  {"velocity", ParamType::Integer, ParamValue::integer(127), ParamValue::integer(0), ParamValue::integer(127), ParamValue::integer(1), "VEL", nullptr, 0, ParamPersist}};
constexpr RgbColor kBase{0,126,129};
constexpr RgbColor kRoot{63,192,0};
constexpr RgbColor kDrum{255,0,0};
constexpr RgbColor kOffScale{0,12,12};
std::uint8_t pad_index(std::uint8_t id) noexcept {
  for (std::uint8_t i=0;i<12;++i) if (music::kPadMap[i] == id) return i;
  return 0;
}
bool append_off(NoteBatch& b, std::uint8_t pitch) noexcept {
  return b.off.append({pitch,0,0}) == PushResult::Added;
}
bool append_on(NoteBatch& b, std::uint8_t pitch, std::uint8_t velocity) noexcept {
  return b.on.append({pitch,velocity,0}) == PushResult::Added;
}
void colors_note(const ProcessContext& c, std::uint8_t layout, InputView& view) noexcept {
  for (std::uint8_t i=0;i<12;++i) view.pad_colors[i] = kBase;
  const auto scale = c.global.scale < music::kScaleLengths.size() ? c.global.scale : 1;
  if (layout == 0) {
    const auto length = music::kScaleLengths[scale];
    for (std::uint8_t idx=0;idx<12;++idx)
      if (((static_cast<std::int64_t>(idx) + c.global.key_offset) % length + length) % length == 0)
        view.pad_colors[pad_index(idx)] = kRoot;
    return;
  }
  for (std::uint8_t pad=0;pad<12;++pad) {
    const auto semitone = static_cast<std::int64_t>(pad) + c.global.key_offset;
    const auto degree = static_cast<std::uint8_t>((semitone % 12 + 12) % 12);
    RgbColor color = kOffScale;
    if (degree == 0) color = kRoot;
    else for (std::uint8_t j=0;j<music::kScaleLengths[scale];++j)
      if (music::kScales[scale][j] == degree) { color = kBase; break; }
    view.pad_colors[pad_index(pad)] = color;
  }
}
void colors_drum(InputView& view) noexcept { for (auto& c : view.pad_colors) c = kDrum; }
std::uint8_t note_pitch(std::uint8_t id, const ProcessContext& c) noexcept {
  const auto p = static_cast<std::int64_t>(pad_index(id)) + c.global.key_offset;
  const auto length = music::kScaleLengths[c.global.scale < music::kScaleLengths.size() ? c.global.scale : 1];
  const auto degree = static_cast<std::uint8_t>((p % length + length) % length);
  const auto octave = p >= 0 ? p / length : (p - (length - 1)) / length;
  auto n = static_cast<std::int64_t>(music::kScales[c.global.scale < music::kScaleLengths.size() ? c.global.scale : 1][degree]) + c.global.key + (octave + c.global.octave + 2) * 12;
  if (n < 0) n = 0;
  if (n > 127) n = 127;
  return static_cast<std::uint8_t>(n);
}
std::uint8_t chromatic_pitch(std::uint8_t id, const ProcessContext& c) noexcept {
  auto n = static_cast<std::int64_t>(c.global.key) + pad_index(id) + c.global.key_offset + (static_cast<std::int64_t>(c.global.octave) + 2) * 12;
  if (n < 0) n = 0;
  if (n > 127) n = 127;
  return static_cast<std::uint8_t>(n);
}
std::uint8_t drum_pitch(std::uint8_t id, const DrumInputState& s, const GlobalState& g) noexcept {
  auto p = pad_index(id);
  if (s.layout == 1) p = static_cast<std::uint8_t>(p + p/3);
  else if (s.layout == 2) p = static_cast<std::uint8_t>(p + p/3 + 1);
  else if (s.layout == 3) p = music::kDrumBottomRows[p];
  else if (s.layout == 4) p = music::kDrumBottomRowsFlipped[p];
  auto n = static_cast<std::int64_t>(p) + s.base_note + g.key_offset;
  if (n < 0) n = 0;
  if (n > 127) n = 127;
  return static_cast<std::uint8_t>(n);
}
template<class State> void release_ids(State& s, const std::uint8_t* ids, std::uint8_t count, NoteBatch& out) noexcept {
  std::uint8_t removed[12]{};
  for (std::uint8_t i=0;i<count;++i) {
    const auto n = s.relationships.remove_all(ids[i],removed,12);
    for (std::uint8_t j=0;j<n;++j) if (!s.relationships.has_output(removed[j])) (void)append_off(out,removed[j]);
  }
}
template<class State> void press_ids(State& s, const std::uint8_t* ids, std::uint8_t count, NoteBatch& out,
                                     std::uint8_t (*convert)(std::uint8_t,void*), void* ctx) noexcept {
  for (std::uint8_t i=0;i<count;++i) {
    const auto pitch = convert(ids[i],ctx);
    if (append_on(out,pitch,s.velocity)) {
      if (!s.relationships.add(ids[i],pitch)) ++s.relationship_overflow;
    }
  }
}
void destroy(InputState&) noexcept {}
const InputDescriptor kNoteDescriptor{"note",1,kNoteParams,4,construct_note_input,destroy,process_note_input,reset_note_input,set_note_input_param,no_input_attributes};
const InputDescriptor kDrumDescriptor{"drum",1,kDrumParams,4,construct_drum_input,destroy,process_drum_input,reset_drum_input,set_drum_input_param,no_input_attributes};
}  // namespace

bool InputRelationships::add(std::uint8_t in, std::uint8_t out) noexcept {
  if (count >= kCapacity) return false;
  input[count]=in; output[count]=out; ++count; return true;
}
std::uint8_t InputRelationships::remove_all(std::uint8_t in, std::uint8_t* removed, std::uint8_t cap) noexcept {
  std::uint8_t n=0, i=0;
  while(i<count) {
    if(input[i]==in) { if(n<cap) removed[n++]=output[i]; for(std::uint8_t j=i+1;j<count;++j){input[j-1]=input[j];output[j-1]=output[j];} --count; }
    else ++i;
  }
  return n;
}
bool InputRelationships::has_output(std::uint8_t out) const noexcept { for(std::uint8_t i=0;i<count;++i) if(output[i]==out) return true; return false; }
void construct_note_input(InputState& s) noexcept { new (&s.storage.note) NoteInputState{}; }
void construct_drum_input(InputState& s) noexcept { new (&s.storage.drum) DrumInputState{}; }
void process_note_input(InputState& state,const ProcessContext& c,const InputFrame& frame,NoteBatch& out,InputView& view) noexcept {
  auto& s=state.storage.note; out={}; colors_note(c,s.layout,view);
  std::uint8_t offs[64]{},ons[64]{},no=0,nn=0;
  for(std::uint16_t i=0;i<frame.keys.count;++i){const auto& e=frame.keys.items[i]; if(!e.valid()) continue; if(e.down){if(nn<64)ons[nn++]=e.key;}else if(no<64)offs[no++]=e.key;}
  release_ids(s,offs,no,out);
  for(std::uint8_t i=0;i<nn;++i){
    if(s.relationships.count>=InputRelationships::kCapacity){++s.relationship_overflow;continue;}
    const auto pitch=s.layout==1?chromatic_pitch(ons[i],c):note_pitch(ons[i],c);
    if(append_on(out,pitch,s.velocity)) (void)s.relationships.add(ons[i],pitch);
  }
}
void process_drum_input(InputState& state,const ProcessContext& c,const InputFrame& frame,NoteBatch& out,InputView& view) noexcept {
  auto& s=state.storage.drum; out={}; colors_drum(view);
  std::uint8_t offs[64]{},ons[64]{},no=0,nn=0;
  for(std::uint16_t i=0;i<frame.keys.count;++i){const auto& e=frame.keys.items[i]; if(!e.valid()) continue; if(e.down){if(nn<64)ons[nn++]=e.key;}else if(no<64)offs[no++]=e.key;}
  release_ids(s,offs,no,out);
  for(std::uint8_t i=0;i<nn;++i){
    if(s.relationships.count>=InputRelationships::kCapacity){++s.relationship_overflow;continue;}
    const auto pitch=drum_pitch(ons[i],s,c.global);
    if(append_on(out,pitch,s.velocity)) (void)s.relationships.add(ons[i],pitch);
  }
}
void reset_note_input(InputState& s,ResetReason r) noexcept { if(r==ResetReason::Stop||r==ResetReason::SceneChange||r==ResetReason::Panic||r==ResetReason::Init) s.storage.note.relationships.clear(); }
void reset_drum_input(InputState& s,ResetReason r) noexcept { if(r==ResetReason::Stop||r==ResetReason::SceneChange||r==ResetReason::Panic||r==ResetReason::Init) s.storage.drum.relationships.clear(); }
bool set_note_input_param(InputState& state,std::uint8_t i,ParamValue v,const GlobalState&,std::uint8_t& effects) noexcept {
  effects=EffectNone; auto& s=state.storage.note;
  if(i==0&&v.type==ParamType::Integer){effects=EffectReleaseNotes|EffectRepaint;return true;}
  if(i==1&&v.type==ParamType::Integer){effects=kReleaseRepaint;return true;}
  if(i==2&&v.type==ParamType::Integer&&v.value.integer>=0&&v.value.integer<=127){s.velocity=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==3&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=1){s.layout=static_cast<std::uint8_t>(v.value.integer);effects=kReleaseRepaint;return true;}
  return false;
}
bool set_drum_input_param(InputState& state,std::uint8_t i,ParamValue v,const GlobalState&,std::uint8_t& effects) noexcept {
  effects=EffectNone; auto& s=state.storage.drum;
  if(i==0&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=4){s.layout=static_cast<std::uint8_t>(v.value.integer);effects=EffectReleaseNotes;return true;}
  if(i==1&&v.type==ParamType::Integer&&v.value.integer>=0&&v.value.integer<=127){s.base_note=static_cast<std::uint8_t>(v.value.integer);effects=EffectReleaseNotes;return true;}
  if(i==2&&v.type==ParamType::Integer){effects=kReleaseRepaint;return true;}
  if(i==3&&v.type==ParamType::Integer&&v.value.integer>=0&&v.value.integer<=127){s.velocity=static_cast<std::uint8_t>(v.value.integer);return true;}
  return false;
}
void no_input_attributes(InputState&,AttributeVisitor&) noexcept {}
const InputDescriptor& note_input_descriptor() noexcept { return kNoteDescriptor; }
const InputDescriptor& drum_input_descriptor() noexcept { return kDrumDescriptor; }
}  // namespace orbit

