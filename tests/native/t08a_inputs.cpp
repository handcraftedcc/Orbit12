#include "domain/modules/input_registry.hpp"
#include "domain/modules/matrix_input.hpp"
#include "domain/music.hpp"
#include <cassert>
#include <cstdint>
#include <new>

namespace {
struct RandomCtx { std::uint32_t n=0; };
std::uint32_t next(void* p) { return ++static_cast<RandomCtx*>(p)->n; }
orbit::ProcessContext context(const orbit::GlobalState& g, RandomCtx& r) {
  static orbit::RandomSource source{nullptr,next}; source={&r,next}; return {{0,0},g,{},source};
}
orbit::InputFrame frame(std::initializer_list<orbit::KeyEvent> events) {
  orbit::InputFrame f{}; for (const auto& e:events) assert(f.keys.append(e)==orbit::PushResult::Added); return f;
}
void note_golden_and_overlap() {
  using namespace orbit;
  InputState s{}; construct_note_input(s); GlobalState g{}; RandomCtx r{}; auto c=context(g,r);
  NoteBatch out{}; InputView view{};
  process_note_input(s,c,frame({{9,true,0},{10,true,0}}),out,view);
  assert(out.on.count==2 && out.on.items[0].pitch==60 && out.on.items[1].pitch==62);
  assert(out.on.items[0].velocity==127 && out.on.items[0].channel==0);
  assert(view.pad_colors[9].red==63 && view.pad_colors[9].green==192);
  process_note_input(s,c,frame({{9,false,1},{10,false,1}}),out,view);
  assert(out.off.count==2 && out.off.items[0].pitch==60 && out.off.items[1].pitch==62);
  assert(s.storage.note.relationships.count==0);
  std::uint8_t effects=0;
  assert(note_input_descriptor().set_param(s,3,ParamValue{ParamType::Enumeration,ParamValue::Scalar(1)},g,effects));
  assert(s.storage.note.layout==1 && (effects&EffectReleaseNotes) && (effects&EffectRepaint));
  g.key_offset=1; auto c2=context(g,r);
  process_note_input(s,c2,frame({{9,true,2}}),out,view);
  assert(out.on.count==1 && out.on.items[0].pitch==61);
  assert(view.pad_colors[9].red==0 && view.pad_colors[9].green==12);
  process_note_input(s,c2,frame({{9,false,3}}),out,view); assert(out.off.count==1 && out.off.items[0].pitch==61);
  assert(note_input_descriptor().set_param(s,0,ParamValue::integer(4),g,effects));
  assert(s.storage.note.relationships.count==0); // Global binding is applied by controller after accepted callback.
  g.octave=4; auto c3=context(g,r); process_note_input(s,c3,frame({{9,true,4}}),out,view); assert(out.on.items[0].pitch==73);
  reset_note_input(s,ResetReason::Stop); assert(s.storage.note.relationships.count==0);
}
void drum_layout_table_and_controls() {
  using namespace orbit;
  InputState s{}; construct_drum_input(s); GlobalState g{}; RandomCtx r{}; auto c=context(g,r);
  NoteBatch out{}; InputView view{}; std::uint8_t effects=0;
  const std::uint8_t expected[5][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},
    {0,1,2,4,5,6,8,9,10,12,13,14},
    {1,2,3,5,6,7,9,10,11,13,14,15},
    {3,7,11,2,6,10,1,5,9,0,4,8},
    {0,4,8,1,5,9,2,6,10,3,7,11}};
  const std::uint8_t ids[12]={9,10,11,6,7,8,3,4,5,0,1,2};
  for(std::uint8_t layout=0;layout<5;++layout){
    assert(drum_input_descriptor().set_param(s,0,ParamValue{ParamType::Enumeration,ParamValue::Scalar(layout)},g,effects));
    assert(effects&EffectReleaseNotes);
    for(std::uint8_t i=0;i<12;++i){ process_drum_input(s,c,frame({{ids[i],true,static_cast<std::uint32_t>(i)}}),out,view); const auto expected_pitch=static_cast<std::uint8_t>(36+expected[layout][i]); assert(out.on.count==1 && out.on.items[0].pitch==expected_pitch); process_drum_input(s,c,frame({{ids[i],false,static_cast<std::uint32_t>(i+1)}}),out,view); assert(out.off.count==1 && out.off.items[0].pitch==expected_pitch); }
  }
  for(const auto& color:view.pad_colors) assert(color.red==255 && color.green==0 && color.blue==0);
  assert(drum_input_descriptor().set_param(s,1,ParamValue::integer(127),g,effects));
  assert(drum_input_descriptor().set_param(s,2,ParamValue::integer(-1),g,effects));
  g.key_offset=-1; auto c2=context(g,r); assert(drum_input_descriptor().set_param(s,3,ParamValue::integer(0),g,effects));
  process_drum_input(s,c2,frame({{9,true,1}}),out,view); assert(out.on.count==1 && out.on.items[0].pitch==126 && out.on.items[0].velocity==0);
  process_drum_input(s,c2,frame({{9,false,2}}),out,view); assert(out.off.count==1 && out.off.items[0].pitch==126);
  assert(!drum_input_descriptor().set_param(s,1,ParamValue::integer(128),g,effects));
  reset_drum_input(s,ResetReason::Stop); assert(s.storage.drum.relationships.count==0);
}
void registries_and_bounds() {
  using namespace orbit;
  assert(input_module_count()>=2 && find_input_module("note")==&note_input_descriptor());
  assert(find_input_module("drum")==&drum_input_descriptor() && find_input_module("matrix")==&matrix_input_descriptor());
  assert(note_input_descriptor().params[0].global_binding==GlobalBinding::Octave);
  assert(note_input_descriptor().params[1].global_binding==GlobalBinding::KeyOffset);
  assert(drum_input_descriptor().params[2].global_binding==GlobalBinding::KeyOffset);
  InputState s{}; construct_note_input(s); GlobalState g{}; RandomCtx r{}; auto c=context(g,r); NoteBatch b{}; InputView v{};
  InputFrame too_many{}; for(std::uint8_t i=0;i<12;++i) assert(too_many.keys.append({i,true,i})==PushResult::Added);
  process_note_input(s,c,too_many,b,v); assert(b.on.count==12 && s.storage.note.relationships.count==12);
  assert(too_many.keys.append({0,true,13})==PushResult::Added); process_note_input(s,c,too_many,b,v);
  // Release-safety exception: refuse ons that cannot retain a release mapping.
  assert(b.on.count==0 && s.storage.note.relationship_overflow==13);
}
}
int main(){note_golden_and_overlap();drum_layout_table_and_controls();registries_and_bounds();}
