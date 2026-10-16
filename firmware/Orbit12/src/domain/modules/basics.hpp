#pragma once
#include "../types.hpp"

namespace orbit {
struct NoteRelationships {
  static constexpr std::uint8_t kCapacity = 12;
  std::uint8_t input[kCapacity]{};
  std::uint8_t output[kCapacity]{};
  std::uint8_t count = 0;
  bool add(std::uint8_t in, std::uint8_t out) noexcept;
  bool remove_first(std::uint8_t in, std::uint8_t& out) noexcept;
  std::uint8_t remove_all(std::uint8_t in, std::uint8_t* removed, std::uint8_t capacity) noexcept;
  bool has_output(std::uint8_t out) const noexcept;
  void clear() noexcept { count = 0; }
};
struct EmptyModuleState { std::uint8_t unused = 0; };
struct TransposeModuleState {
  std::int32_t semitones = 0;
  std::int32_t octaves = 0;
  bool scale_aware = true;
  NoteRelationships relationships{};
  std::uint32_t relationship_overflow = 0;
};
struct ChordModuleState {
  std::int32_t offsets[5]{0,2,4,0,0};
  bool scale_aware = true;
  NoteRelationships relationships{};
  std::uint32_t relationship_overflow = 0;
  std::uint32_t output_overflow = 0;
};
struct DropModuleState {
  std::int32_t drop = 0;             // DROP 0..100
  std::uint8_t mode = 0;             // MDE: 0=NOTE, 1=STEP
  std::int32_t pattern_length = 0;   // PTLEN 0..128
  std::int32_t seed = 0;             // SEED 0..10000
};
struct RandomizeModuleState {
  NoteRelationships relationships{};
  std::int32_t velocity_range = 0;
  std::uint8_t velocity_mode = 0;
  std::int32_t note_range = 0;
  std::int32_t note_increment = 1;
  float note_chance = 0.5f;
  std::uint8_t note_mode = 0;
  bool scale_aware = true;
  std::int32_t octave_range = 0;
  float octave_chance = 0.5f;
  std::uint8_t octave_mode = 0;
  std::int32_t pattern_length = 0;
  std::int32_t random_seed_user = 0;
  std::uint32_t relationship_overflow = 0;
};
// Delay/bounce release bookkeeping. Counters are diagnostics only; they are
// never read by the audio path.
struct TimingModuleCounters {
  std::uint32_t on_overflow = 0;          // pending note-on refused (source drops silently)
  std::uint32_t delay_unrecorded = 0;     // note-delay record refused (source drops silently)
  std::uint32_t release_immediate = 0;    // release queue full: release sent now
  std::uint32_t pending_on_cancelled = 0; // its pending note-on was cancelled instead of sounding
  std::uint32_t release_full_skipped = 0; // bounce note or repeat skipped: release queue full
  std::uint32_t repeat_skipped = 0;       // bounce note skipped while output was full
  std::uint32_t output_full = 0;          // bounce note-on or repeat dropped by output capacity
};
// on_id identifies the note-on this release ends (0 when unknown). A release is
// never sent in the same call as its own note-on: flush sends offs first.
struct ScheduledOff {
  std::uint8_t pitch = 0;
  std::uint8_t channel = 0;
  std::uint32_t due = 0;  // 32-bit milliseconds or MIDI ticks, compared modularly
  std::uint32_t on_id = 0;
};
struct ScheduledOffs {
  ScheduledOff items[kPolyphony]{};
  std::uint8_t count = 0;
};
struct TimedNote {
  Note note{};
  std::uint32_t due = 0;
  std::uint32_t id = 0;
};
struct DelayNoteTime {
  std::uint8_t pitch = 0;
  std::int64_t delay = 0;
  std::uint32_t id = 0;
};
struct DelayModuleState {
  std::int32_t delay = 100;        // DLY (ms)
  std::int32_t delay_random = 0;   // DLRND (ms)
  TimedNote ons[kPolyphony]{};
  std::uint8_t on_count = 0;
  ScheduledOffs offs{};
  DelayNoteTime note_delays[kPolyphony]{};
  std::uint8_t note_delay_count = 0;
  std::uint32_t next_id = 1;
  TimingModuleCounters counters{};
};
struct BounceRepeat {
  TimedNote note{};
  std::uint8_t remaining = 0;
};
struct BounceModuleState {
  std::int32_t bounces = 3;            // BOUNCE 0..64
  std::uint8_t interval_mode = 0;      // MODE: 0 MS, 1 RATE
  std::int32_t interval_ms = 150;      // INTMS
  std::uint8_t interval_rate_value = 8;  // INTRT index into music::kRateMidiTicks
  std::int32_t interval_rate = 6;      // MIDI ticks for interval_rate_value
  double gate = 100.0;                 // GATE (ms)
  double interval_mod = 0.0;           // INTMOD
  double velocity_mod = 1.0;           // VELMOD
  double gate_mod = 0.0;               // GTEMOD
  std::uint8_t mod_curve = 0;          // MODCRV: 0 LIN, 1 CUBIC, 2 RAND
  BounceRepeat repeats[kPolyphony]{};
  std::uint8_t repeat_count = 0;
  ScheduledOffs offs{};
  TimingModuleCounters counters{};
};
struct GateScheduledOff {
  Note note{};
  std::uint32_t due_ms = 0;  // Same clock as TimeSample::ms; compared with wrap-safe deltas.
};
struct GateModuleState {
  std::int32_t gate = 100;          // GATE (source: unbounded integer)
  std::int32_t gate_random = 0;     // GTRND (source: unbounded integer)
  std::int32_t pattern_length = 0;  // PTLEN 0..128
  GateScheduledOff scheduled[kPolyphony]{};
  std::uint8_t scheduled_count = 0;
  std::uint32_t schedule_overflow = 0;  // Note-ons refused because their off could not be scheduled.
};
struct ChopperModuleState {
  std::uint8_t rate_index = 8;       // RATE 0..12, index into music::kRateMidiTicks
  float chop = 0.5f;                 // CHOP
  std::int32_t pattern_length = 16;  // PTLEN 1..128
  std::int32_t seed = 0;             // SEED 0..10000 (Parm name rand_seed)
  std::uint8_t retrigger_mode = 0;   // RTRG: 0=STBL, 1=CONT, 2=RTRG
  NoteBuffer held{};                 // Held notes with velocity, oldest first.
  NoteBuffer active{};               // Notes currently sounding through the gate.
  bool blocked = false;
  std::uint64_t step_index = 0;
  std::int64_t last_grid_bin = -1;
  std::uint32_t output_overflow = 0;  // Ons refused or retriggers skipped for batch capacity.
};
struct RetriggerModuleState {
  std::uint8_t mode = 0;          // MODE: 0=PRESS, 1=REL, 2=BOTH
  std::int32_t threshold = 50;    // THRSH 0..1000 (milliseconds)
  bool has_last_event = false;
  std::uint32_t last_event_ms = 0;
  NoteBuffer held{};
  std::uint32_t output_overflow = 0;  // Retriggers skipped because their off batch would overflow.
  std::uint32_t held_overflow = 0;    // Held notes not recorded (source drops them silently).
};
// Euclid lanes: Python LANES = 6. Pitch kNoPitch marks an idle lane.
constexpr std::size_t kEuclidLanes = 6;
struct EuclidModuleState {
  std::uint8_t lane = 0;                 // LANE 0..5: lane addressed by PULSES, LENGTH and ROT
  std::uint8_t rate_index = 8;           // RATE 0..12, index into music::kRateMidiTicks
  std::uint8_t sort_mode = 0;            // SORT: 0=UP, 1=DOWN, 2=ORD
  std::uint8_t retrigger_mode = 0;       // RTRG: 0=STBL, 1=CONT, 2=RTRG
  float gate = 100.0f;                   // GATE ms, 0..100000; the deadline uses int(gate)
  std::uint8_t pulses[kEuclidLanes]{4, 4, 4, 4, 4, 4};       // PULSES per lane, 0..128
  std::uint8_t lengths[kEuclidLanes]{16, 16, 16, 16, 16, 16};  // LENGTH per lane, 1..128
  std::int32_t rotations[kEuclidLanes]{};                    // ROT per lane, -127..127
  NoteBuffer held{};                     // Held notes with velocity, oldest first.
  NoteBuffer note_order{};               // Held notes in SORT order for the current pass.
  std::uint8_t active_pitch[kEuclidLanes]{255, 255, 255, 255, 255, 255};  // Sounding pitch per lane.
  std::uint32_t active_due[kEuclidLanes]{};  // Gate deadline (ms), compared with wrap-safe deltas.
  std::uint64_t step_index = 0;          // Counter for CONT and RTRG steps.
  std::int64_t last_grid_bin = -1;       // Last grid bin; -1 before the grid starts.
  bool was_running = false;
  std::uint32_t held_overflow = 0;       // Held notes not recorded (source drops them silently).
};
// Arp: source note_register (12), held_notes (12), scheduled_offs (12). popped_ids and the
// output arrays are not state: due releases are compacted in place and output is the batch.
struct ArpScheduled {
  Note note{};
  std::uint32_t due = 0;  // Same clock as TimeSample::ms; compared with wrap-safe deltas.
};
struct ArpModuleState {
  std::uint8_t rate_index = 8;        // RATE 0..12, index into music::kRateMidiTicks
  float gate = 100.0f;                // GATE 0..100000 ms; truncated with int() at use
  std::uint8_t sort_mode = 0;         // SORT: 0 UP, 1 DOWN, 2 ORD
  std::uint8_t play_mode = 0;         // PLAY: 0 SORT .. 8 RPT
  std::int32_t transpose_steps = 0;   // TRNS -24..24
  std::int32_t max_transpose = 0;     // MAXTRN 0..48
  std::uint8_t boundary_mode = 0;     // BND: 0 WRPU .. 4 CLMP
  std::uint8_t pattern = 0;           // RYTM 0..24, index into music::kPatternBits
  std::int32_t pattern_shift = 0;     // RYSHFT (source unbounded; int32 here)
  std::int32_t random_pattern_length = 0;  // RNDLEN 0..128
  std::int32_t random_seed_user = 0;  // SEED 0..10000
  std::uint8_t retrigger_mode = 0;    // RTRG: 0 RTRG, 1 CONT, 2 STBL
  NoteBuffer held{};                  // Performer holds by pitch, oldest first.
  NoteBuffer notes{};                 // Note register: unique pitches, velocity of first press.
  ArpScheduled scheduled[kPolyphony]{};
  std::uint8_t scheduled_count = 0;
  std::uint8_t arp_state = 0;         // 1 once the register has held a note
  bool was_running = false;
  std::int64_t last_grid_bin = -1;
  std::uint64_t position = 0;         // note_register_position
  std::uint64_t step = 0;             // active_pattern_step
  std::uint32_t held_overflow = 0;    // Holds not recorded (source drops silently).
  std::uint32_t register_overflow = 0;  // Notes not added to the register (source drops silently).
  std::uint32_t schedule_overflow = 0;  // Emissions refused: schedule full (deviation 1).
  std::uint32_t output_overflow = 0;    // Emissions refused: note-on batch full.
};
// Latch: source held_notes (12). Pitches are unique; a note-on for a latched pitch releases it.
struct LatchModuleState {
  std::int32_t polyphony = 12;        // POLY 1..12
  NoteBuffer held{};                  // Latched notes (pitch, velocity, channel), oldest first.
  std::uint32_t held_overflow = 0;    // Latches refused (unreachable after eviction; counted if it occurs).
  std::uint32_t output_overflow = 0;  // Note-ons refused: batch or release room full; nothing latched.
};
// Pick: source held_notes_in (12), active_notes (12); next_notes is rebuilt in each pass.
struct PickModuleState {
  std::int32_t count = 1;             // COUNT 1..12
  std::int32_t offset = 0;            // OFST 0..12
  std::int32_t octaves = 0;           // OCT (source: unbounded integer; int32 here)
  std::uint8_t pick_mode = 0;         // MDE: 0 STRT, 1 END, 2 LOW, 3 HIGH
  bool invert = false;                // INV
  NoteBuffer held{};                  // Held note-ons, oldest first; duplicate pitches allowed.
  NoteBuffer active{};                // Notes sent and not yet released by this module.
  std::uint32_t held_overflow = 0;    // Note-ons not recorded: 12 held (source drops silently).
  std::uint32_t range_skipped = 0;    // Selected notes outside MIDI 0..127 after OCT (latch_pick deviation 3).
  std::uint32_t output_overflow = 0;  // Emissions refused: batch full (unreachable with 12-entry bounds).
};
// Quantize: source pending_ons (12, with velocity and grid time), scheduled_offs (12, with off
// time), note_delays (12, times 1..96) and held_notes (12). Times are unwrapped 64-bit ticks.
struct QuantizeEntry {
  Note note{};
  std::int64_t due = 0;
};
struct QuantizeDelay {
  std::uint8_t pitch = 0;
  std::uint8_t delay = 0;
};
struct QuantizeModuleState {
  std::uint8_t rate_index = 8;      // RATE 0..12, index into music::kRateMidiTicks
  bool tick_known = false;
  std::uint32_t last_tick = 0;      // Last transport tick (uint32), for signed deltas
  std::int64_t tick = 0;            // Unwrapped transport tick (source midi_tick)
  QuantizeEntry pending[kPolyphony]{};
  std::uint8_t pending_count = 0;
  QuantizeEntry scheduled[kPolyphony]{};
  std::uint8_t scheduled_count = 0;
  QuantizeDelay delays[kPolyphony]{};
  std::uint8_t delay_count = 0;
  NoteBuffer held{};                // Held notes; velocity unused, channel from the note-on
  std::uint32_t pending_evicted = 0;   // Oldest pending note-on dropped (source drops silently)
  std::uint32_t delay_unrecorded = 0;  // Note-delay record refused (source drops silently)
  std::uint32_t held_overflow = 0;     // Held note not recorded (defensive; unreachable)
};

// Strum: source note_register (12), strum timing and counters. Tilt is float32 (see strum.cpp).
struct StrumModuleState {
  std::int32_t amount = 30;            // AMT 0..1000000 ms
  std::uint8_t order = 0;              // ORDR 0 ORD, 1 UP, 2 DOWN, 3 UPDWN, 4 RAND
  float tilt = 0.0f;                   // TILT -1..1
  std::int32_t tilt_note_count = 3;    // CNT 1..12 (stored; the source does not use it)
  std::uint8_t articulate = 0;         // ARTC 0..8
  NoteBuffer register_notes{};         // Pending chord tones, oldest first
  bool strum_active = false;
  std::uint32_t next_strum_ms = 0;     // Next emission time, wrap-safe 32-bit milliseconds
  std::uint8_t strum_position = 0;     // 0..4 within the strum
  std::int64_t strum_count = 0;        // Completed strums (seed for RAND and UPDWN)
  std::uint32_t register_overflow = 0; // Chord tones not registered (source drops silently)
};

// Arp walk: source note_register (12), held_notes (12), scheduled_offs (12) and the walker state.
// step_values is the saved attribute (fixed eight elements, source MAX_STEPS).
struct ArpWalkModuleState {
  std::uint8_t rate_index = 8;          // RATE 0..12, index into music::kRateMidiTicks
  std::uint8_t sort_mode = 0;           // SORT: 0 UP, 1 DOWN, 2 ORD
  std::uint8_t step_length = 2;         // LEN 1..8
  std::uint8_t step_select = 0;         // SEL 0..step_length-1
  std::int32_t step_values[8]{-1, 2, 0, 0, 0, 0, 0, 0};  // Saved attribute; VAL -24..24
  std::uint64_t random_counter = 0;     // RAND presses (source unbounded; not saved)
  std::uint32_t last_tick = 0;          // Transport tick of the latest process pass (randomize seed)
  bool invert = false;                  // INV
  std::int32_t max_walk = 0;            // MAXWLK 0..48
  std::uint8_t boundary_mode = 0;       // BND: 0 WRPU .. 4 CLMP
  float gate = 100.0f;                  // GATE 0..100000 ms; truncated with int() at use
  std::uint8_t pattern = 0;             // RYTM 0..24, index into music::kPatternBits
  std::int32_t pattern_shift = 0;       // RYSHFT (source unbounded; int32 here)
  std::uint8_t retrigger_mode = 0;      // RTRG: 0 RTRG, 1 CONT, 2 STBL
  NoteBuffer held{};                    // Performer holds by pitch, oldest first.
  NoteBuffer notes{};                   // Note register: unique pitches, velocity of first press.
  ArpScheduled scheduled[kPolyphony]{};
  std::uint8_t scheduled_count = 0;
  std::uint8_t arp_state = 0;           // 1 once the register has held a note
  bool was_running = false;
  std::int64_t last_grid_bin = -1;
  std::int64_t walk_position = 0;       // walk_position (source unbounded)
  std::uint64_t step_position = 0;      // step_position: index into step_values
  std::uint64_t active_step = 0;        // active_pattern_step
  std::uint32_t held_overflow = 0;      // Holds not recorded (source drops silently).
  std::uint32_t register_overflow = 0;  // Notes not added to the register (source drops silently).
  std::uint32_t schedule_overflow = 0;  // Emissions refused: schedule full (deviation 1).
  std::uint32_t output_overflow = 0;    // Emissions refused: note-on batch full (deviation 1).
  std::uint32_t walk_limit = 0;         // Chord walk corrections cut at the cap (deviation 6).
  std::uint32_t tail_fast_forward = 0;  // Passes with triggers beyond the explicit window (deviation 5).
};

// Wander: source center_note, current_note, held_notes (12), walk counters and the single sounding
// note. Gate is float, truncated with int() at use (see wander.cpp for the bound).
struct WanderModuleState {
  std::uint8_t rate_index = 8;         // RATE 0..12, index into music::kRateMidiTicks
  std::int32_t gravity = 50;           // GRAV 0..100
  std::int32_t min_step = 1;           // MIN 0..16
  std::int32_t max_step = 2;           // MAX 0..16
  std::int32_t max_deviation = 8;      // DEV 0..24
  std::int32_t tension = 0;            // TENS 0..100
  std::int32_t random_seed_user = 0;   // SEED 0..10000 (Parm name rand_seed)
  std::int32_t loop_length = 0;        // LOOP 0..128
  std::uint8_t retrigger_mode = 0;     // RTRG: 0 STBL, 1 CONT, 2 RTRG
  std::uint8_t pattern = 0;            // RTHM 0..24, index into music::kPatternBits
  float gate = 100.0f;                 // GATE ms, clamped to -100000..100000 (deviation W3)
  NoteBuffer held{};                   // Held notes, oldest first (pitch, velocity, channel)
  std::uint8_t center_note = 60;
  std::uint8_t current_note = 60;
  std::uint8_t last_velocity = 127;
  std::uint64_t step_index = 0;        // CONT and RTRG step counter (source unbounded)
  std::int64_t last_grid_bin = -1;
  bool was_running = false;
  bool has_active = false;
  Note active{};                       // Sounding note; its release repeats pitch and channel
  std::uint32_t active_due = 0;        // Release deadline (ms), compared with wrap-safe deltas
  bool tick_known = false;
  std::uint32_t last_tick = 0;         // Last transport tick (uint32), for signed deltas
  std::int64_t tick = 0;               // Unwrapped transport tick (source midi_tick)
  std::uint32_t held_overflow = 0;     // Holds not recorded (source drops silently)
  std::uint32_t output_overflow = 0;   // Emissions refused: batch full (unreachable with 1 on/1 off per pass)
};

struct ModuleState {
  union Storage {
    EmptyModuleState empty;
    TransposeModuleState transpose;
    ChordModuleState chord;
    DropModuleState drop;
    RandomizeModuleState randomize;
    GateModuleState gate;
    ChopperModuleState chopper;
    RetriggerModuleState retrigger;
    DelayModuleState delay;
    BounceModuleState bounce;
    EuclidModuleState euclid;
    ArpModuleState arp;
    ArpWalkModuleState arp_walk;
    LatchModuleState latch;
    PickModuleState pick;
    QuantizeModuleState quantize;
    StrumModuleState strum;
    WanderModuleState wander;
    constexpr Storage() : empty{} {}
  } storage;
  constexpr ModuleState() : storage{} {}
};
void construct_empty_state(ModuleState& state) noexcept;
void construct_transpose_state(ModuleState& state) noexcept;
void construct_chord_state(ModuleState& state) noexcept;
void process_empty(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_transpose(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void process_chord(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&) noexcept;
void reset_empty_state(ModuleState&, ResetReason) noexcept;
void reset_transpose_state(ModuleState&, ResetReason) noexcept;
void reset_chord_state(ModuleState&, ResetReason) noexcept;
bool set_transpose_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
bool set_chord_param(ModuleState&, std::uint8_t, ParamValue, const GlobalState&, std::uint8_t&) noexcept;
void empty_attributes(ModuleState&, AttributeVisitor&) noexcept;
const ModuleDescriptor& transpose_module_descriptor() noexcept;
const ModuleDescriptor& chord_module_descriptor() noexcept;
const ModuleDescriptor& empty_module_descriptor() noexcept;
}  // namespace orbit
