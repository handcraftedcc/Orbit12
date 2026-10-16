#include "chords_input.hpp"
#include "../music.hpp"
#include <new>
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace orbit {
namespace {
constexpr const char* kLayoutOptions[] = {"SPLT", "4&8"};
constexpr const char* kModifierOptions[] = {"OFF","INV1","INV2","INV3","SEV","ADD9","SUS2","SUS4","PWR","DIM","BRRW"};
constexpr const char* kBassOptions[] = {"NONE","ROOT","2ND","LOW","HIGH"};
constexpr const char* kSpreadOptions[] = {"TIGHT","MED","WIDE"};
constexpr const char* kBorrowOptions[] = {"AUTO","MAJ","MIN","HMIN","MMIN","ION","DOR","PHR","LYD","MIX","AEO","LOC","MAJP","MINP","SPEN","BLU","MIN6","M7B","WT","DIMH","DIMW"};
constexpr std::int32_t kIntMin = INT32_MIN, kIntMax = INT32_MAX;
constexpr std::uint8_t kReleaseRepaint = EffectReleaseNotes | EffectRepaint;
const ParamSpec kParams[] = {
  {"octave",ParamType::Integer,ParamValue::integer(3),ParamValue::integer(kIntMin),ParamValue::integer(kIntMax),ParamValue::integer(1),"OCT",nullptr,0,ParamPersist,GlobalBinding::Octave},
  {"key_offset",ParamType::Integer,ParamValue::integer(0),ParamValue::integer(kIntMin),ParamValue::integer(kIntMax),ParamValue::integer(1),"PADOFS",nullptr,0,ParamPersist,GlobalBinding::KeyOffset},
  {"modifier_select",ParamType::Integer,ParamValue::integer(1),ParamValue::integer(1),ParamValue::integer(6),ParamValue::integer(1),"SEL",nullptr,0,ParamPersist},
  {"modifier_action",ParamType::Enumeration,ParamValue::integer(4),ParamValue::integer(0),ParamValue::integer(10),ParamValue::integer(1),"MOD",kModifierOptions,11,ParamPersist},
  {"bass",ParamType::Enumeration,ParamValue::integer(0),ParamValue::integer(0),ParamValue::integer(4),ParamValue::integer(1),"BASS",kBassOptions,5,ParamPersist},
  {"spread",ParamType::Enumeration,ParamValue::integer(0),ParamValue::integer(0),ParamValue::integer(2),ParamValue::integer(1),"SPRD",kSpreadOptions,3,ParamPersist},
  {"borrow_scale",ParamType::Enumeration,ParamValue::integer(0),ParamValue::integer(0),ParamValue::integer(20),ParamValue::integer(1),"BRWSCL",kBorrowOptions,21,ParamPersist},
  {"max_chords",ParamType::Integer,ParamValue::integer(1),ParamValue::integer(1),ParamValue::integer(6),ParamValue::integer(1),"MAXCT",nullptr,0,ParamPersist},
  {"velocity",ParamType::Integer,ParamValue::integer(127),ParamValue::integer(0),ParamValue::integer(127),ParamValue::integer(1),"VEL",nullptr,0,ParamPersist},
  {"layout",ParamType::Enumeration,ParamValue::integer(0),ParamValue::integer(0),ParamValue::integer(1),ParamValue::integer(1),"LAY",kLayoutOptions,2,ParamPersist},
  {"split_drop",ParamType::Integer,ParamValue::integer(7),ParamValue::integer(1),ParamValue::integer(7),ParamValue::integer(1),"SPLDRP",nullptr,0,ParamPersist}};
constexpr std::uint8_t kRoots[2][8]={{0,1,2,3,4,5,0,0},{1,2,4,5,7,8,10,11}};
constexpr std::uint8_t kRootCount[2]={6,8};
constexpr std::uint8_t kMods[2][6]={{6,7,8,9,10,11},{0,3,6,9,0,0}};
constexpr std::uint8_t kModCount[2]={6,4};
constexpr RgbColor kBase{0,126,129}, kRoot{63,192,0}, kModifier{192,63,0};
std::uint8_t floor_degree(std::int64_t v,std::uint8_t n,std::int64_t& octave) noexcept {
  octave=v/n; auto degree=v%n; if(degree<0){degree+=n;--octave;} return static_cast<std::uint8_t>(degree);
}
std::uint8_t root_degree(std::uint8_t slot,const ChordInputState& s,std::uint8_t scale_len) noexcept {
  auto d=static_cast<std::int32_t>(slot);
  if(s.layout==0&&scale_len==7){const auto drop=static_cast<std::uint8_t>((s.split_drop<1?1:(s.split_drop>7?7:s.split_drop))-1);if(slot>=drop)++d;}
  return static_cast<std::uint8_t>(d);
}
bool find_slot(const std::uint8_t* values,std::uint8_t count,std::uint8_t key,std::uint8_t& slot) noexcept {
  for(std::uint8_t i=0;i<count;++i) {
    if(values[i]==key) { slot=i; return true; }
  }
  return false;
}
void set_colors(const ChordInputState& s,const ProcessContext& c,InputView& view) noexcept {
  for(auto& color:view.pad_colors)color=kBase;
  const auto scale=c.global.scale<music::kScaleLengths.size()?c.global.scale:1;
  const auto length=music::kScaleLengths[scale];
  for(std::uint8_t i=0;i<kRootCount[s.layout];++i){auto d=static_cast<std::int64_t>(root_degree(i,s,length))+c.global.key_offset;if(((d%length)+length)%length==0){for(std::uint8_t p=0;p<12;++p)if(music::kPadMap[p]==kRoots[s.layout][i])view.pad_colors[p]=kRoot;}}
  for(std::uint8_t i=0;i<kModCount[s.layout];++i)for(std::uint8_t p=0;p<12;++p)if(music::kPadMap[p]==kMods[s.layout][i])view.pad_colors[p]=kModifier;
}
void remove_modifier(ChordInputState& s,std::uint8_t slot) noexcept {
  for(std::uint8_t i=0;i<s.held_modifier_count;++i)if(s.held_modifiers[i]==slot){for(std::uint8_t j=i+1;j<s.held_modifier_count;++j)s.held_modifiers[j-1]=s.held_modifiers[j];--s.held_modifier_count;return;}
}
void remove_root(ChordInputState& s,std::uint8_t root,NoteBatch& out) noexcept {
  bool has=false; for(std::uint8_t i=0;i<s.relationships.count;++i)if(s.relationships.input[i]==root){has=true;break;} if(!has)return;
  std::uint8_t removed[12]{};const auto n=s.relationships.remove_all(root,removed,12);
  if(s.held_chords) --s.held_chords;
  for(std::uint8_t i=0;i<n;++i)if(!s.relationships.has_output(removed[i])) (void)out.off.append({removed[i],0,0});
}
struct Mods {std::uint8_t inversion=0,sus=0;bool sev=false,add9=false,power=false,borrow=false,dim=false;};
Mods resolve(const ChordInputState& s) noexcept {
  Mods m{};for(std::uint8_t i=0;i<s.held_modifier_count;++i){const auto slot=s.held_modifiers[i];if(slot>=6)continue;const auto a=s.modifier_actions[slot];if(a==0)continue;if(a<4){m.inversion=m.inversion==0?a:(m.inversion<a?m.inversion:a);}else if(a==4)m.sev=true;else if(a==5)m.add9=true;else if(a==6||a==7)m.sus=a;else if(a==8)m.power=true;else if(a==9)m.dim=true;else m.borrow=true;}return m;
}
void build_chord(ChordInputState& s,const ProcessContext& c,std::uint8_t slot,Mods m) noexcept {
  s.temp_chord_count=0;
  const auto scale_id=c.global.scale<music::kScaleLengths.size()?c.global.scale:1;
  const auto used_scale=m.borrow?(s.borrow_scale==0?music::kAutoBorrowRelationship[scale_id]:s.borrow_scale):scale_id;
  const auto* notes=&music::kScales[used_scale];const auto length=music::kScaleLengths[used_scale];
  const auto source_length=music::kScaleLengths[scale_id];
  std::int64_t root_oct=0;const auto root_pad=static_cast<std::int64_t>(root_degree(slot,s,length))+c.global.key_offset;
  const auto rdegree=floor_degree(root_pad,length,root_oct);
  const bool major_pent=scale_id==12||scale_id==14,minor_pent=scale_id==13;
  std::int32_t second=rdegree+(major_pent?1:(minor_pent?2:2));
  std::int32_t third=rdegree+(major_pent?3:(minor_pent?3:4));
  if(m.sus==6)second=rdegree+1;else if(m.sus==7)second=rdegree+3;
  s.temp_chord[s.temp_chord_count++]=rdegree;s.temp_chord[s.temp_chord_count++]=static_cast<std::uint8_t>(second);s.temp_chord[s.temp_chord_count++]=static_cast<std::uint8_t>(third);
  if(m.sev)s.temp_chord[s.temp_chord_count++]=static_cast<std::uint8_t>(rdegree+((scale_id==12||scale_id==13||scale_id==14)?4:6));
  if(m.add9)s.temp_chord[s.temp_chord_count++]=static_cast<std::uint8_t>(rdegree+((scale_id==12||scale_id==13||scale_id==14)?6:8));
  for(std::uint8_t i=0;i<s.temp_chord_count;++i){std::int64_t oct=0;const auto deg=floor_degree(s.temp_chord[i],length,oct);auto n=static_cast<std::int64_t>((*notes)[deg])+c.global.key+(oct+c.global.octave+2+root_oct)*12;if(n<0)n=0;if(n>127)n=127;s.temp_chord[i]=static_cast<std::uint8_t>(n);}
  const auto root=s.temp_chord[0];
  if(m.dim){if(m.sus==0&&s.temp_chord_count>1&&s.temp_chord[1])--s.temp_chord[1];if(s.temp_chord_count>2&&s.temp_chord[2])--s.temp_chord[2];}
  const auto inv=(m.inversion<s.temp_chord_count-1)?m.inversion:static_cast<std::uint8_t>(s.temp_chord_count-1);
  for(std::uint8_t i=0;i<inv;++i)s.temp_chord[i]=static_cast<std::uint8_t>(s.temp_chord[i]>115?127:s.temp_chord[i]+12);
  if(s.spread_mode){auto i=static_cast<std::uint8_t>(s.temp_chord_count-1);s.temp_chord[i]=static_cast<std::uint8_t>(s.temp_chord[i]>115?127:s.temp_chord[i]+12);if(s.spread_mode==2&&s.temp_chord_count>1){i=static_cast<std::uint8_t>(s.temp_chord_count-2);s.temp_chord[i]=static_cast<std::uint8_t>(s.temp_chord[i]>115?127:s.temp_chord[i]+12);}}
  if(m.power&&s.temp_chord_count>1){for(std::uint8_t i=2;i<s.temp_chord_count;++i)s.temp_chord[i-1]=s.temp_chord[i];--s.temp_chord_count;}
  if(s.bass_mode){std::uint8_t bass=root;
    if(s.bass_mode==2&&s.temp_chord_count>1)bass=s.temp_chord[1];
    else if(s.bass_mode==3){for(std::uint8_t i=1;i<s.temp_chord_count;++i)if(s.temp_chord[i]<bass)bass=s.temp_chord[i];}
    else if(s.bass_mode==4){for(std::uint8_t i=1;i<s.temp_chord_count;++i)if(s.temp_chord[i]>bass)bass=s.temp_chord[i];}
    if(s.temp_chord_count<6){for(std::uint8_t i=s.temp_chord_count;i>0;--i)s.temp_chord[i]=s.temp_chord[i-1];s.temp_chord[0]=bass>=12?static_cast<std::uint8_t>(bass-12):0;++s.temp_chord_count;}
  }
  (void)source_length;
}
void destroy(InputState&) noexcept {}
const InputDescriptor kDescriptor{"chords",1,kParams,11,construct_chords_input,destroy,process_chords_input,reset_chords_input,set_chords_input_param,no_input_attributes};
static_assert(std::is_trivially_destructible<ChordInputState>::value,"Input-state union members must have trivial destruction");
}
void construct_chords_input(InputState& state) noexcept { new (&state.storage.chords) ChordInputState{}; }
void process_chords_input(InputState& state,const ProcessContext& c,const InputFrame& frame,NoteBatch& out,InputView& view) noexcept {
  auto& s=state.storage.chords;out={};set_colors(s,c,view);
  std::uint8_t ons[kInputEventCapacity]{},offs[kInputEventCapacity]{},on_count=0,off_count=0;
  for(std::uint16_t i=0;i<frame.keys.count;++i){const auto& e=frame.keys.items[i];if(!e.valid())continue;if(e.down){if(on_count<kInputEventCapacity)ons[on_count++]=e.key;}else if(off_count<kInputEventCapacity)offs[off_count++]=e.key;}
  std::uint8_t pending_offs[6]{},po=0;
  for(std::uint8_t i=0;i<off_count;++i){const auto pad=static_cast<std::uint8_t>(std::find(music::kPadMap.begin(),music::kPadMap.end(),offs[i])-music::kPadMap.begin());std::uint8_t slot=0;if(find_slot(kRoots[s.layout],kRootCount[s.layout],pad,slot)){bool held=false, duplicate=false;
      for(std::uint8_t j=0;j<s.relationships.count;++j) if(s.relationships.input[j]==slot) held=true;
      for(std::uint8_t j=0;j<po;++j) if(pending_offs[j]==slot) duplicate=true;
      if(held&&!duplicate&&po<6)pending_offs[po++]=slot;}else if(find_slot(kMods[s.layout],kModCount[s.layout],pad,slot))remove_modifier(s,slot);}
  for(std::uint8_t i=0;i<po;++i)remove_root(s,pending_offs[i],out);
  std::uint8_t new_chords=0;
  for(std::uint8_t i=0;i<on_count;++i){const auto pad=static_cast<std::uint8_t>(std::find(music::kPadMap.begin(),music::kPadMap.end(),ons[i])-music::kPadMap.begin());std::uint8_t slot=0;if(find_slot(kRoots[s.layout],kRootCount[s.layout],pad,slot))++new_chords;}
  auto force=static_cast<std::int16_t>(new_chords+s.held_chords)-s.max_chords;
  if(force>0){std::uint8_t seen[12]{},seen_count=0,release[6]{},release_count=0;for(std::uint8_t i=0;i<s.relationships.count&&force>0;++i){const auto root=s.relationships.input[i];bool duplicate=false;for(std::uint8_t j=0;j<seen_count;++j)if(seen[j]==root)duplicate=true;if(duplicate)continue;seen[seen_count++]=root;bool already=false;for(std::uint8_t j=0;j<po;++j)if(pending_offs[j]==root)already=true;if(already)continue;if(release_count<6){release[release_count++]=root;--force;}}
    for(std::uint8_t i=0;i<release_count;++i)remove_root(s,release[i],out);
  }
  std::uint8_t pending_roots[6]{},pr=0;
  for(std::uint8_t i=0;i<on_count;++i){const auto pad=static_cast<std::uint8_t>(std::find(music::kPadMap.begin(),music::kPadMap.end(),ons[i])-music::kPadMap.begin());std::uint8_t slot=0;if(find_slot(kRoots[s.layout],kRootCount[s.layout],pad,slot)){if(pr<s.max_chords)pending_roots[pr++]=slot;continue;}if(find_slot(kMods[s.layout],kModCount[s.layout],pad,slot)){if(s.follow_modifier_keys&&slot!=s.modifier_pad)s.modifier_pad=slot;bool held=false;for(std::uint8_t j=0;j<s.held_modifier_count;++j)if(s.held_modifiers[j]==slot)held=true;if(!held&&s.held_modifier_count<6)s.held_modifiers[s.held_modifier_count++]=slot;}}
  const auto mods=resolve(s);
  for(std::uint8_t i=0;i<pr;++i){build_chord(s,c,pending_roots[i],mods);std::uint8_t added=0;for(std::uint8_t j=0;j<s.temp_chord_count;++j){const auto pitch=s.temp_chord[j];if(s.relationships.count>=InputRelationships::kCapacity){++s.relationship_overflow;break;}if(out.on.count>=kPolyphony){++s.output_overflow;break;}if(s.relationships.has_output(pitch))(void)out.off.append({pitch,0,0});if(!s.relationships.add(pending_roots[i],pitch)){++s.relationship_overflow;break;}if(out.on.append({pitch,s.velocity,0})!=PushResult::Added){++s.output_overflow;std::uint8_t rem[12]{};(void)s.relationships.remove_all(pending_roots[i],rem,12);break;}++added;}if(added)++s.held_chords;}
}
void reset_chords_input(InputState& state,ResetReason r) noexcept {if(r==ResetReason::Stop||r==ResetReason::SceneChange||r==ResetReason::Panic||r==ResetReason::Init){auto& s=state.storage.chords;s.held_modifier_count=0;s.relationships.clear();s.held_chords=0;s.temp_chord_count=0;s.follow_modifier_keys=false;}}
bool set_chords_input_param(InputState& state,std::uint8_t i,ParamValue v,const GlobalState&,std::uint8_t& effects) noexcept {
  auto& s=state.storage.chords;effects=EffectNone;
  if(i<2&&v.type==ParamType::Integer){effects=kReleaseRepaint;return true;}
  if(i==2&&v.type==ParamType::Integer&&v.value.integer>=1&&v.value.integer<=6){s.modifier_pad=static_cast<std::uint8_t>(v.value.integer-1);return true;}
  if(i==3&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=10){s.modifier_actions[s.modifier_pad]=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==4&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=4){s.bass_mode=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==5&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=2){s.spread_mode=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==6&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=20){s.borrow_scale=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==7&&v.type==ParamType::Integer&&v.value.integer>=1&&v.value.integer<=6){s.max_chords=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==8&&v.type==ParamType::Integer&&v.value.integer>=0&&v.value.integer<=127){s.velocity=static_cast<std::uint8_t>(v.value.integer);return true;}
  if(i==9&&v.type==ParamType::Enumeration&&v.value.integer>=0&&v.value.integer<=1){s.layout=static_cast<std::uint8_t>(v.value.integer);s.held_modifier_count=0;effects=kReleaseRepaint;return true;}
  if(i==10&&v.type==ParamType::Integer&&v.value.integer>=1&&v.value.integer<=7){s.split_drop=static_cast<std::uint8_t>(v.value.integer);effects=EffectRepaint;return true;}
  return false;
}
void set_chords_modifier_follow(InputState& s,bool enabled) noexcept {s.storage.chords.follow_modifier_keys=enabled;}
void chords_input_follow_modifier_keys(InputState& s,bool enabled) noexcept {set_chords_modifier_follow(s,enabled);}
const InputDescriptor& chords_input_descriptor() noexcept {return kDescriptor;}
} // namespace orbit
