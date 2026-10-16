#include "modules/common/registry.hpp"
#include <cassert>
#include <cstring>
#include <limits>

namespace {
bool equal(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
orbit::ProcessContext context(orbit::GlobalState& global, orbit::RandomSource& random) {
  return {{10,10000}, global, {}, random};
}
}
int main() {
  using namespace orbit;
  GlobalState global{}; std::uint32_t seed=0;
  RandomSource random{&seed, [](void* p) { return ++*static_cast<std::uint32_t*>(p); }};
  auto ctx=context(global, random);
  const auto* empty=find_module("empty");
  const auto* transpose=find_module("transpose");
  const auto* chord=find_module("chord");
  assert(empty && transpose && chord && !find_module("phraser"));
  assert(transpose->version==1 && transpose->param_count==3);
  assert(equal(transpose->params[0].name,"semitones") && transpose->params[0].default_value.value.integer==0);
  assert(equal(transpose->params[1].name,"octaves") && transpose->params[1].default_value.value.integer==0);
  assert(equal(transpose->params[2].name,"scale_aware") && transpose->params[2].default_value.value.boolean);
  assert(chord->version==1 && chord->param_count==6);
  const std::int32_t defaults[]{0,2,4,0,0};
  for (std::uint8_t i=0;i<5;++i) assert(chord->params[i].default_value.value.integer==defaults[i]);
  assert(chord->params[5].default_value.value.boolean);

  ModuleState state{}; empty->construct(state);
  NoteBatch in{}; in.on.append({60,91,2}); in.off.append({60,0,2}); NoteBatch out{};
  empty->process(state,ctx,in,out);
  assert(out.on.count==1 && out.on.items[0].pitch==60 && out.on.items[0].velocity==91 && out.on.items[0].channel==2);
  assert(out.off.count==1 && out.off.items[0].pitch==60);

  transpose->construct(state);
  in={}; in.on.append({60,100,0}); in.on.append({61,80,0}); in.on.append({127,70,0});
  transpose->process(state,ctx,in,out);
  assert(out.on.count==3 && out.on.items[0].pitch==60 && out.on.items[1].pitch==60 && out.on.items[2].pitch==127);
  in={}; in.off.append({60,0,0}); in.off.append({61,0,0}); in.off.append({127,0,0});
  transpose->process(state,ctx,in,out);
  assert(out.off.count==3 && out.off.items[0].pitch==60 && out.off.items[1].pitch==60 && out.off.items[2].pitch==127);

  std::uint8_t effects=0;
  assert(transpose->set_param(state,0,ParamValue::integer(2),global,effects));
  assert(transpose->set_param(state,1,ParamValue::integer(1),global,effects));
  assert(transpose->set_param(state,2,ParamValue::boolean(false),global,effects));
  in={}; in.on.append({60,111,4}); transpose->process(state,ctx,in,out);
  assert(out.on.count==1 && out.on.items[0].pitch==74 && out.on.items[0].velocity==111);
  in={}; in.off.append({60,0,4}); transpose->process(state,ctx,in,out);
  assert(out.off.count==1 && out.off.items[0].pitch==74 && out.off.items[0].channel==4);
  // int32 controls use wider intermediates before MIDI saturation.
  assert(transpose->set_param(state,0,ParamValue::integer((std::numeric_limits<std::int32_t>::min)()),global,effects));
  assert(transpose->set_param(state,1,ParamValue::integer((std::numeric_limits<std::int32_t>::min)()),global,effects));
  in={}; in.on.append({60,100,0}); transpose->process(state,ctx,in,out);
  assert(out.on.count==1 && out.on.items[0].pitch==0);

  assert(transpose->set_param(state,0,ParamValue::integer(-1),global,effects));
  assert(transpose->set_param(state,1,ParamValue::integer(0),global,effects));
  assert(transpose->set_param(state,2,ParamValue::boolean(true),global,effects));
  in={}; in.on.append({60,100,0}); transpose->process(state,ctx,in,out);
  assert(out.on.items[0].pitch==59);
  transpose->reset(state,ResetReason::Stop);
  in={}; in.off.append({60,0,0}); transpose->process(state,ctx,in,out);
  assert(out.off.count==0);

  // The source mapping has 12 entries. The release-safety exception refuses
  // an additional on rather than emitting a tone whose later off cannot map.
  in={};
  for (std::uint8_t note=0; note<12; ++note) in.on.append({note,100,0});
  transpose->process(state,ctx,in,out);
  assert(out.on.count==12);
  in={}; in.on.append({50,100,0});
  transpose->process(state,ctx,in,out);
  assert(out.on.count==0 && state.storage.transpose.relationship_overflow==1);
  in={};
  for (std::uint8_t note=0; note<12; ++note) in.off.append({note,0,0});
  transpose->process(state,ctx,in,out);
  assert(out.off.count==12 && state.storage.transpose.relationships.count==0);

  chord->construct(state);
  in={}; in.on.append({60,100,0}); chord->process(state,ctx,in,out);
  assert(out.on.count==3 && out.on.items[0].pitch==60 && out.on.items[1].pitch==64 && out.on.items[2].pitch==67);
  in={}; in.off.append({60,0,0}); chord->process(state,ctx,in,out);
  assert(out.off.count==3 && out.off.items[0].pitch==60 && out.off.items[1].pitch==64 && out.off.items[2].pitch==67);

  // Shared chord tones are retriggered at the source and released only after
  // the final input relationship is removed.
  in={}; in.on.append({60,90,0}); in.on.append({64,80,0}); chord->process(state,ctx,in,out);
  assert(out.on.count==6 && out.off.count==2);
  in={}; in.off.append({60,0,0}); chord->process(state,ctx,in,out);
  assert(out.off.count==1 && out.off.items[0].pitch==60);
  in={}; in.off.append({64,0,0}); chord->process(state,ctx,in,out);
  assert(out.off.count==3 && out.off.items[0].pitch==64 && out.off.items[1].pitch==67 && out.off.items[2].pitch==71);

  assert(chord->set_param(state,0,ParamValue::integer(0),global,effects));
  assert(chord->set_param(state,1,ParamValue::integer(2),global,effects));
  assert(chord->set_param(state,2,ParamValue::integer(4),global,effects));
  assert(chord->set_param(state,3,ParamValue::integer(0),global,effects));
  assert(chord->set_param(state,4,ParamValue::integer(0),global,effects));
  assert(chord->set_param(state,5,ParamValue::boolean(false),global,effects));
  in={}; in.on.append({60,100,0}); chord->process(state,ctx,in,out);
  assert(out.on.count==3 && out.on.items[0].pitch==60 && out.on.items[1].pitch==62 && out.on.items[2].pitch==64);
  chord->reset(state,ResetReason::Stop);

  // Relationship storage remains fixed at 12 fanout entries. Chord stops
  // generating untracked tones when a fifth triad would exceed that bound.
  assert(chord->set_param(state,5,ParamValue::boolean(true),global,effects));
  in={}; for (std::uint8_t n=0;n<5;++n) in.on.append({static_cast<std::uint8_t>(48+n*2),100,0});
  chord->process(state,ctx,in,out);
  assert(out.on.count==12 && state.storage.chord.relationships.count==12);
  assert(state.storage.chord.relationship_overflow==3);
  chord->reset(state,ResetReason::Stop);
  assert(state.storage.chord.relationships.count==0);
}
