#include "modules/core/chords/chords_input.hpp"
#include "modules/common/input_registry.hpp"
#include "domain/music.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace {
struct Rng { std::uint32_t value=0; };
std::uint32_t draw(void* p){return ++static_cast<Rng*>(p)->value;}
orbit::ProcessContext make_context(const orbit::GlobalState& g,Rng& r){static orbit::RandomSource random{nullptr,draw};random={&r,draw};return {{0,0},g,{},random};}
orbit::InputFrame events(std::initializer_list<orbit::KeyEvent> es){orbit::InputFrame f{};for(const auto& e:es)assert(f.keys.append(e)==orbit::PushResult::Added);return f;}
void apply(orbit::InputState& s,const orbit::InputDescriptor& d,std::uint8_t i,orbit::ParamValue v,const orbit::GlobalState& g){std::uint8_t effects=0;assert(d.set_param(s,i,v,g,effects));}
void expect(const orbit::NoteBatch& b,const std::uint8_t* notes,std::uint8_t n){assert(b.on.count==n);for(std::uint8_t i=0;i<n;++i)assert(b.on.items[i].pitch==notes[i]);}
void default_chord_release_and_parameter_schema(){
  using namespace orbit;InputState s{};construct_chords_input(s);GlobalState g{};Rng r{};auto c=make_context(g,r);NoteBatch b{};InputView v{};const auto& d=chords_input_descriptor();
  assert(d.param_count==11&&d.params[0].global_binding==GlobalBinding::Octave&&d.params[1].global_binding==GlobalBinding::KeyOffset);
  assert(d.params[2].default_value.value.integer==1&&d.params[3].default_value.value.integer==4);
  assert(s.storage.chords.modifier_actions[0]==4&&s.storage.chords.modifier_actions[1]==7&&s.storage.chords.modifier_actions[2]==8&&s.storage.chords.modifier_actions[3]==5&&s.storage.chords.modifier_actions[4]==1&&s.storage.chords.modifier_actions[5]==10);
  process_chords_input(s,c,events({{9,true,0}}),b,v);const std::uint8_t triad[]={60,64,67};expect(b,triad,3);assert(b.on.items[0].velocity==127&&b.on.items[0].channel==0);assert(v.pad_colors[9].red==63&&v.pad_colors[9].green==192);assert(v.pad_colors[3].red==192&&v.pad_colors[3].green==63);
  process_chords_input(s,c,events({{9,false,1}}),b,v);assert(b.off.count==3&&b.off.items[0].pitch==60&&b.off.items[1].pitch==64&&b.off.items[2].pitch==67&&s.storage.chords.held_chords==0);
  assert(d.params[2].minimum.value.integer==1&&d.params[2].maximum.value.integer==6);
  assert(d.params[3].option_count==11&&d.params[4].option_count==5&&d.params[5].option_count==3&&d.params[6].option_count==21);
  assert(d.params[7].minimum.value.integer==1&&d.params[7].maximum.value.integer==6);
  assert(d.params[8].minimum.value.integer==0&&d.params[8].maximum.value.integer==127);
  assert(d.params[9].option_count==2&&d.params[10].minimum.value.integer==1&&d.params[10].maximum.value.integer==7);
  std::uint8_t effects=0;assert(d.set_param(s,0,ParamValue::integer(-2),g,effects)&&(effects&EffectReleaseNotes));
  assert(d.set_param(s,1,ParamValue::integer(-3),g,effects)&&(effects&EffectReleaseNotes));g.octave=4;g.key_offset=1;auto c2=make_context(g,r);process_chords_input(s,c2,events({{9,true,2}}),b,v);assert(b.on.count==3&&b.on.items[0].pitch==74);
  reset_chords_input(s,ResetReason::Stop);assert(s.storage.chords.relationships.count==0&&s.storage.chords.held_modifier_count==0&&s.storage.chords.held_chords==0);
}
void all_modifier_actions_are_source_mapped(){
  using namespace orbit;const std::uint8_t expected_count[11]={3,3,3,3,4,4,3,3,2,3,3};
  const std::uint8_t expected[11][4]={{60,64,67,0},{72,64,67,0},{72,76,67,0},{72,76,67,0},{60,64,67,71},{60,64,67,74},{60,62,67,0},{60,65,67,0},{60,67,0,0},{60,63,66,0},{60,63,67,0}};
  for(std::uint8_t action=0;action<11;++action){InputState s{};construct_chords_input(s);GlobalState g{};Rng r{};auto c=make_context(g,r);NoteBatch b{};InputView v{};const auto& d=chords_input_descriptor();apply(s,d,2,ParamValue::integer(1),g);apply(s,d,3,ParamValue{ParamType::Enumeration,ParamValue::Scalar(action)},g);
    // SPLT modifier physical pad 6 is note ID PADMAP[6]=3; root pad 0 is id 9.
    process_chords_input(s,c,events({{3,true,0}}),b,v);assert(b.on.count==0);
    process_chords_input(s,c,events({{9,true,1}}),b,v);assert(b.on.count==expected_count[action]);for(std::uint8_t i=0;i<b.on.count;++i)assert(b.on.items[i].pitch==expected[action][i]);
    process_chords_input(s,c,events({{9,false,2}}),b,v);assert(b.off.count==expected_count[action]);
    process_chords_input(s,c,events({{3,false,3}}),b,v);assert(s.storage.chords.held_modifier_count==0);
  }
}
void bass_spread_borrow_and_split_layout(){
  using namespace orbit;InputState s{};construct_chords_input(s);GlobalState g{};Rng r{};auto c=make_context(g,r);NoteBatch b{};InputView v{};const auto& d=chords_input_descriptor();
  apply(s,d,4,ParamValue{ParamType::Enumeration,ParamValue::Scalar(1)},g);process_chords_input(s,c,events({{9,true,0}}),b,v);const std::uint8_t bass[]={48,60,64,67};expect(b,bass,4);process_chords_input(s,c,events({{9,false,1}}),b,v);
  apply(s,d,4,ParamValue{ParamType::Enumeration,ParamValue::Scalar(2)},g);process_chords_input(s,c,events({{9,true,2}}),b,v);const std::uint8_t second[]={52,60,64,67};expect(b,second,4);process_chords_input(s,c,events({{9,false,3}}),b,v);
  apply(s,d,4,ParamValue{ParamType::Enumeration,ParamValue::Scalar(3)},g);process_chords_input(s,c,events({{9,true,31}}),b,v);const std::uint8_t lowest[]={48,60,64,67};expect(b,lowest,4);process_chords_input(s,c,events({{9,false,32}}),b,v);
  apply(s,d,4,ParamValue{ParamType::Enumeration,ParamValue::Scalar(4)},g);process_chords_input(s,c,events({{9,true,33}}),b,v);const std::uint8_t highest[]={55,60,64,67};expect(b,highest,4);process_chords_input(s,c,events({{9,false,34}}),b,v);
  apply(s,d,4,ParamValue{ParamType::Enumeration,ParamValue::Scalar(0)},g);apply(s,d,5,ParamValue{ParamType::Enumeration,ParamValue::Scalar(1)},g);process_chords_input(s,c,events({{9,true,4}}),b,v);const std::uint8_t med[]={60,64,79};expect(b,med,3);process_chords_input(s,c,events({{9,false,5}}),b,v);
  apply(s,d,5,ParamValue{ParamType::Enumeration,ParamValue::Scalar(2)},g);process_chords_input(s,c,events({{9,true,6}}),b,v);const std::uint8_t wide[]={60,76,79};expect(b,wide,3);process_chords_input(s,c,events({{9,false,7}}),b,v);
  apply(s,d,5,ParamValue{ParamType::Enumeration,ParamValue::Scalar(0)},g);apply(s,d,2,ParamValue::integer(6),g);apply(s,d,3,ParamValue{ParamType::Enumeration,ParamValue::Scalar(10)},g);apply(s,d,6,ParamValue{ParamType::Enumeration,ParamValue::Scalar(13)},g);process_chords_input(s,c,events({{2,true,8}}),b,v); // modifier slot 5, selected BRRW
  process_chords_input(s,c,events({{9,true,9}}),b,v);const std::uint8_t borrowed[]={60,65,70};expect(b,borrowed,3);process_chords_input(s,c,events({{9,false,10}}),b,v);process_chords_input(s,c,events({{2,false,11}}),b,v);
  // Split drop 1 maps root slot 0 upward one scale degree.
  apply(s,d,3,ParamValue{ParamType::Enumeration,ParamValue::Scalar(0)},g);apply(s,d,9,ParamValue{ParamType::Enumeration,ParamValue::Scalar(0)},g);apply(s,d,10,ParamValue::integer(1),g);g.key_offset=0;g.octave=3;auto c2=make_context(g,r);process_chords_input(s,c2,events({{9,true,12}}),b,v);const std::uint8_t shifted[]={62,65,69};expect(b,shifted,3);
  process_chords_input(s,c,events({{9,false,13}}),b,v);apply(s,d,9,ParamValue{ParamType::Enumeration,ParamValue::Scalar(1)},g);process_chords_input(s,c,events({{10,true,14}}),b,v);assert(b.on.count==3&&b.on.items[0].pitch==60); // first 4&8 root (pad 1), root slot 0
}
void cap_oldest_relationship_reset_and_modifier_follow(){
  using namespace orbit;InputState s{};construct_chords_input(s);GlobalState g{};Rng r{};auto c=make_context(g,r);NoteBatch b{};InputView v{};const auto& d=chords_input_descriptor();apply(s,d,7,ParamValue::integer(2),g);
  process_chords_input(s,c,events({{9,true,0}}),b,v);process_chords_input(s,c,events({{10,true,1}}),b,v);assert(s.storage.chords.held_chords==2);
  process_chords_input(s,c,events({{11,true,2}}),b,v);assert(b.off.count==3&&b.off.items[0].pitch==60&&b.on.count==3&&b.on.items[0].pitch==64);
  process_chords_input(s,c,events({{10,false,3}}),b,v);assert(b.off.count==3&&s.storage.chords.held_chords==1);
  apply(s,d,7,ParamValue::integer(6),g);InputFrame many{};for(std::uint8_t i=0;i<6;++i)assert(many.keys.append({9,true,i})==PushResult::Added);process_chords_input(s,c,many,b,v);assert(b.on.count==12&&s.storage.chords.relationships.count==12&&s.storage.chords.relationship_overflow>0);
  reset_chords_input(s,ResetReason::Stop);assert(s.storage.chords.relationships.count==0&&s.storage.chords.held_chords==0);
  set_chords_modifier_follow(s,false);process_chords_input(s,c,events({{2,true,5}}),b,v);assert(s.storage.chords.modifier_pad==0);
  set_chords_modifier_follow(s,true);process_chords_input(s,c,events({{2,true,6}}),b,v);assert(s.storage.chords.modifier_pad==5);
  process_chords_input(s,c,events({{9,true,7}}),b,v);const std::uint8_t auto_borrow[]={60,63,67};expect(b,auto_borrow,3); // selected slot 5 is BRRW; source auto-borrow maps Major to Minor.
  reset_chords_input(s,ResetReason::SceneChange);assert(s.storage.chords.relationships.count==0&&s.storage.chords.held_modifier_count==0);
}
bool saw_attribute(void* p,orbit::AttributeDescriptor&){++*static_cast<std::uint8_t*>(p);return true;}
void registry_and_no_save_attrs(){using namespace orbit;assert(input_module_count()==6);assert(find_input_module("chords")==&chords_input_descriptor());assert(input_module_at(2)==&chords_input_descriptor());InputState s{};construct_chords_input(s);assert(chords_input_descriptor().attributes);std::uint8_t count=0;AttributeVisitor visitor{&count,saw_attribute};chords_input_descriptor().attributes(s,visitor);assert(count==0);}
}
int main(){default_chord_release_and_parameter_schema();all_modifier_actions_are_source_mapped();bass_spread_borrow_and_split_layout();cap_oldest_relationship_reset_and_modifier_follow();registry_and_no_save_attrs();}


