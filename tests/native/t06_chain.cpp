#include "domain/chain.hpp"
#include <cassert>

namespace {
struct MidiCapture {
  orbit::MidiPacket packets[32]{};
  std::uint8_t count = 0;
  static bool send(void* context, const orbit::MidiPacket& packet) {
    auto& capture = *static_cast<MidiCapture*>(context);
    capture.packets[capture.count++] = packet;
    return true;
  }
};
void construct(orbit::ModuleState& s) { s.storage.empty = {}; }
void destroy(orbit::ModuleState&) {}
void reset(orbit::ModuleState&, orbit::ResetReason) {}
bool set(orbit::ModuleState&, std::uint8_t, orbit::ParamValue, const orbit::GlobalState&, std::uint8_t&) { return false; }
void attrs(orbit::ModuleState&, orbit::AttributeVisitor&) {}
template <std::uint8_t Shift>
void shift(orbit::ModuleState&, const orbit::ProcessContext&, const orbit::NoteBatch& input, orbit::NoteBatch& output) {
  for (std::uint16_t i = 0; i < input.off.count; ++i) {
    auto n = input.off.items[i]; n.pitch = static_cast<std::uint8_t>(n.pitch + Shift); output.off.append(n);
  }
  for (std::uint16_t i = 0; i < input.on.count; ++i) {
    auto n = input.on.items[i]; n.pitch = static_cast<std::uint8_t>(n.pitch + Shift); output.on.append(n);
  }
}
void duplicate(orbit::ModuleState&, const orbit::ProcessContext&, const orbit::NoteBatch& input, orbit::NoteBatch& output) {
  for (std::uint16_t i = 0; i < input.on.count; ++i) { output.on.append(input.on.items[i]); output.on.append(input.on.items[i]); }
}
void thirteen_offs(orbit::ModuleState&, const orbit::ProcessContext&, const orbit::NoteBatch&, orbit::NoteBatch& output) {
  for (std::uint8_t i = 0; i < 13; ++i) (void)output.off.append({static_cast<std::uint8_t>(40 + i), 0, 0});  // module ignores Full
}
constexpr orbit::ModuleDescriptor off13{"test_off13",1,nullptr,0,construct,destroy,thirteen_offs,reset,set,attrs};
constexpr orbit::ModuleDescriptor plus1{"test_plus1",1,nullptr,0,construct,destroy,shift<1>,reset,set,attrs};
constexpr orbit::ModuleDescriptor plus2{"test_plus2",1,nullptr,0,construct,destroy,shift<2>,reset,set,attrs};
constexpr orbit::ModuleDescriptor plus3{"test_plus3",1,nullptr,0,construct,destroy,shift<3>,reset,set,attrs};
constexpr orbit::ModuleDescriptor pass{"test_pass",1,nullptr,0,construct,destroy,shift<0>,reset,set,attrs};
constexpr orbit::ModuleDescriptor dup{"test_dup",1,nullptr,0,construct,destroy,duplicate,reset,set,attrs};
}

int main() {
  using namespace orbit;
  GlobalState global{}; std::uint32_t seed = 0;
  RandomSource random{&seed, [](void* p) { return ++*static_cast<std::uint32_t*>(p); }};
  ProcessContext context{{1,1000},global,{},random};
  NoteBatch input{}; input.on.append({60,100,0}); input.off.append({59,0,0});
  Chain chain;
  assert(module_count() == 18 && find_module("empty") == &empty_module_descriptor());
  assert(module_at(0) == &empty_module_descriptor() && module_at(16) == find_module("transpose"));
  assert(find_module("phraser") == nullptr); // Registry-only Python label remains hidden.
  assert(chain.set_module(0, &plus1)); assert(chain.set_module(1, &plus2));
  assert(chain.set_module(2, &plus3)); assert(chain.set_module(3, &pass));
  assert(chain.set_module(4, &pass)); assert(chain.set_module(5, &pass));
  assert(chain.configure(0, ChainSource::Input, ChainOperation::Next, 0));
  assert(chain.configure(1, ChainSource::Slot1, ChainOperation::OutNext, 9));
  assert(chain.configure(2, ChainSource::Previous, ChainOperation::OutSource, 3));
  assert(chain.configure(3, ChainSource::Previous, ChainOperation::Add, 0));
  assert(chain.configure(4, ChainSource::Previous, ChainOperation::Skip, 0));
  assert(chain.configure(5, ChainSource::Previous, ChainOperation::OutNext, 0));
  assert(!chain.configure(0, ChainSource::Slot1, ChainOperation::Next, 0)); // future/self tap
  OutputCollector collector;
  chain.process(context, input, collector);
  assert(collector.off_count() == 3 && collector.on_count() == 3);
  assert(collector.overflowed() == false);

  // OutNext routes transformed notes and continues them; OutSource routes its
  // transformed notes but continues the selected source. ADD preserves both
  // processed and source multiplicity; SKIP passes through unchanged.
  for (std::uint16_t i=0;i<collector.off_count();++i) assert(collector.off_at(i).channel == (i == 0 ? 9 : (i == 1 ? 3 : 0)));
  assert(collector.off_at(0).note.pitch == 62 && collector.off_at(1).note.pitch == 65);
  assert(collector.on_at(0).channel == 9 && collector.on_at(0).note.pitch == 63);
  assert(collector.on_at(1).channel == 3 && collector.on_at(1).note.pitch == 66);
  assert(collector.on_at(2).channel == 0 && collector.on_at(2).note.pitch == 63);
  assert(chain.final_stream().on.count == 2 && chain.final_stream().off.count == 2);
  assert(chain.final_stream().on.items[0].pitch == 63 && chain.final_stream().on.items[1].pitch == 63);

  // Every documented source selector (PREV, IN, S1..S5) resolves to the
  // current input or a previously completed slot snapshot.
  Chain taps;
  for (std::uint8_t i=0;i<6;++i) assert(taps.set_module(i, &plus1));
  for (std::uint8_t i=0;i<5;++i) assert(taps.configure(i, ChainSource::Previous, ChainOperation::Next, 0));
  for (std::uint8_t source=0;source<=6;++source) {
    assert(taps.configure(5, static_cast<ChainSource>(source), ChainOperation::OutNext, source));
    OutputCollector routed; taps.process(context, input, routed);
    const std::uint8_t expected = source == 0 ? 66 : (source == 1 ? 61 : static_cast<std::uint8_t>(60 + source));
    assert(routed.on_count() == 1 && routed.on_at(0).note.pitch == expected);
    assert(routed.on_at(0).channel == source);
  }
  Chain stale;
  assert(stale.set_module(0, &plus1) && stale.set_module(1, &plus2));
  assert(stale.configure(0, ChainSource::Previous, ChainOperation::Next, 0));
  assert(stale.configure(1, ChainSource::Slot1, ChainOperation::OutNext, 4));
  OutputCollector first_pass; stale.process(context, input, first_pass);
  assert(stale.final_stream().on.items[0].pitch == 63);
  assert(stale.configure(0, ChainSource::Previous, ChainOperation::Skip, 0));
  NoteBatch changed{}; changed.on.append({70,100,0});
  OutputCollector skipped_pass; stale.process(context, changed, skipped_pass);
  assert(skipped_pass.on_count() == 1 && skipped_pass.on_at(0).note.pitch == 63);
  assert(stale.final_stream().on.items[0].pitch == 63);

  // OUT+S snapshots the processor result for future taps while returning its
  // selected source to the continuing stream.
  Chain out_source_tap;
  assert(out_source_tap.set_module(0, &plus1) && out_source_tap.set_module(1, &plus2) && out_source_tap.set_module(2, &pass));
  assert(out_source_tap.configure(0, ChainSource::Previous, ChainOperation::Next, 0));
  assert(out_source_tap.configure(1, ChainSource::Input, ChainOperation::OutSource, 1));
  assert(out_source_tap.configure(2, ChainSource::Slot2, ChainOperation::OutNext, 2));
  OutputCollector out_source_records; out_source_tap.process(context, input, out_source_records);
  assert(out_source_records.on_count() == 2);
  assert(out_source_records.on_at(0).note.pitch == 62 && out_source_records.on_at(1).note.pitch == 62);
  assert(out_source_tap.final_stream().on.items[0].pitch == 62);

  // Module flow preserves duplicate notes while output scheduling deduplicates
  // repeated pitch/channel identities like the Python OutputManager.
  OutputCollector duplicates; NoteBatch many{};
  for (int i=0;i<6;++i) many.on.append({static_cast<std::uint8_t>(i),100,0});
  assert(duplicates.append(many, 2));
  Chain dup_chain; assert(dup_chain.set_module(0, &dup));
  for (std::uint8_t i=0;i<5;++i) assert(dup_chain.set_module(static_cast<std::uint8_t>(i+1), &pass));
  for (std::uint8_t i=0;i<6;++i) assert(dup_chain.configure(i, i ? ChainSource::Previous : ChainSource::Input,
      i == 0 ? ChainOperation::OutNext : ChainOperation::Skip, 2));
  OutputCollector duplicate_output; dup_chain.process(context, many, duplicate_output);
  assert(duplicate_output.on_count() == 6);
  assert(dup_chain.final_stream().on.count == 12);

  // Each output kind has the source array capacity of 24. Overflow is visible,
  // and clearing the collector clears its fault indication as well.
  OutputCollector saturated; NoteBatch twelve{};
  for (std::uint8_t i=0;i<12;++i) { twelve.on.append({i,90,0}); twelve.off.append({i,0,0}); }
  assert(saturated.append(twelve, 0) && saturated.append(twelve, 1));
  assert(!saturated.append(twelve, 2) && saturated.overflowed());
  saturated.clear(); assert(!saturated.overflowed() && saturated.on_count() == 0);

  Chain overflow_chain;
  for (std::uint8_t i=0;i<6;++i) {
    assert(overflow_chain.set_module(i, &pass));
    assert(overflow_chain.configure(i, i ? ChainSource::Previous : ChainSource::Input,
                                    ChainOperation::OutNext, i));
  }
  OutputCollector lost_routes; overflow_chain.process(context, twelve, lost_routes);
  assert(overflow_chain.recovery_required() && lost_routes.overflowed());
  Output already_playing; MidiCapture played;
  NoteBatch held{}; held.on.append({100,100,0});
  assert(already_playing.submit(held,0)); assert(already_playing.flush({&played,&MidiCapture::send})==1);
  assert(already_playing.held_count()==1);
  assert(!lost_routes.drain(already_playing));
  assert(!lost_routes.overflowed() && already_playing.pending()>=2);
  assert(already_playing.flush({&played,&MidiCapture::send})>=2 && already_playing.held_count()==0);
  assert(played.packets[1].kind==MidiKind::NoteOff && played.packets[2].kind==MidiKind::Stop);

  // Collector ordering: slot A press then slot B release of the same key must
  // drain in append order (Python flushed all offs first and stuck the note).
  {
    Output o; MidiCapture cap; MidiPort pt{&cap, &MidiCapture::send};
    NoteBatch on70{}; on70.on.append({70,100,0});
    NoteBatch off70{}; off70.off.append({70,0,0});
    assert(o.submit(on70,0) && o.flush(pt)==1 && o.is_sounding(0,70));
    OutputCollector c; assert(c.append(on70,0) && c.append(off70,0));
    assert(c.on_count()==1 && c.off_count()==1);  // opposite event between: nothing collapsed
    assert(c.drain(o) && o.flush(pt)==1);
    assert(cap.packets[1].kind==MidiKind::NoteOff && !o.is_sounding(0,70) && o.pending()==0);
    // Press from A, release from B, press from C: net a single press.
    OutputCollector d; assert(d.append(on70,0) && d.append(off70,0) && d.append(on70,0));
    assert(d.on_count()==2 && d.drain(o) && o.pending()==1 && o.flush(pt)==1 && o.is_sounding(0,70));
  }

  // Sticky overflow flag: set by a refused append, cleared by clear()/reassignment,
  // untouched by normal use and by Invalid notes.
  {
    NoteBatch b{};
    for (std::uint8_t i=0;i<12;++i) assert(b.off.append({i,0,0})==PushResult::Added);
    assert(!b.overflowed() && sizeof(NoteBuffer) <= 40);
    assert(b.off.append({50,0,0})==PushResult::Full && b.off.overflowed && b.overflowed() && !b.on.overflowed);
    NoteBatch copy = b; assert(copy.overflowed());
    b.off.clear(); assert(!b.overflowed()); b = copy; b = {}; assert(!b.overflowed());
    NoteBatch inv{}; assert(append_note(inv.on,{200,0,0})==PushResult::Invalid && !inv.overflowed());
  }

  // Normal passes never set the flag or recover.
  {
    Chain ok; assert(ok.set_module(0,&plus1) && ok.configure(0,ChainSource::Input,ChainOperation::OutNext,0));
    OutputCollector c; ok.process(context,input,c);
    assert(!ok.recovery_required() && !c.overflowed() && !ok.final_stream().overflowed());
    assert(ok.diagnostics().batch_overflow==0 && ok.diagnostics().scratch_overflow==0);
  }

  // A module that ignores a refused off-append (13 offs into 12 slots) forces the
  // exact recovery: counted, collector faulted, and nothing remains sounding.
  for (int mode = 0; mode < 3; ++mode) {  // 0: NEXT, 1: OUT+N, 2: input slot overflow
    Output o; MidiCapture cap; MidiPort pt{&cap, &MidiCapture::send};
    NoteBatch notes{};
    for (std::uint8_t i=0;i<13;++i) notes.on.append({static_cast<std::uint8_t>(40+i % 12),100,0});
    notes.on.count = 12;
    assert(o.submit(notes,0) && o.flush(pt)==12 && o.held_count()==12);
    Chain chain2; OutputCollector c;
    NoteBatch in{}; in.on.append({1,100,0});
    if (mode == 2) { for (std::uint8_t i=0;i<13;++i) (void)in.off.append({static_cast<std::uint8_t>(40+i),0,0}); assert(in.overflowed()); }
    assert(chain2.set_module(0,&off13) && chain2.configure(0,ChainSource::Input,mode==1?ChainOperation::OutNext:ChainOperation::Next,0));
    chain2.process(context,in,c);
    assert(chain2.recovery_required() && c.overflowed() && chain2.diagnostics().batch_overflow==1);
    assert(!chain2.final_stream().overflowed());
    chain2.process(context,in,c);  // skipped pass still keeps the fault visible
    assert(c.overflowed());
    assert(!c.drain(o));
    for (int guard=0; guard<8 && (o.pending() || o.held_count()); ++guard) (void)o.flush(pt);
    assert(o.held_count()==0 && o.owed_count()==0 && o.pending()==0);
    std::uint8_t offs=0; for (std::uint8_t i=0;i<cap.count;++i) offs += cap.packets[i].kind==MidiKind::NoteOff;
    assert(offs==12);
  }
}
