# Module contracts and interface baseline

This is the normative interface shape for T01 to materialize as headers. Names may be adjusted locally by Sol before dependent tasks start; semantic changes require a decision record. Domain headers include only standard fixed-size/type utilities, never Arduino headers.

```cpp
namespace orbit {
constexpr size_t kKeys = 12, kProcessors = 6, kPolyphony = 12;
constexpr size_t kOutputCapacity = 24;
struct TimeSample { uint32_t ms, us; };
struct Note { uint8_t pitch, velocity, channel; }; // pitch/velocity 0..127, channel 0..15
enum class PushResult : uint8_t { Added, Duplicate, Full, Invalid };
template<class T, size_t N> struct FixedBuffer {
  T items[N]; uint16_t count = 0;
  PushResult push(const T&); // never allocates; no overwrite on Full
  void clear();
};
struct NoteBatch {
  FixedBuffer<Note,kPolyphony> off, on;
};
struct KeyEvent { uint8_t key; bool down; uint32_t at_ms; };
struct InputFrame {
  FixedBuffer<KeyEvent,64> keys;
  int16_t encoder_delta;
  bool encoder_down, encoder_pressed, encoder_released;
};
struct GlobalState; // exact Python SET defaults and ranges, validated setters
struct TransportView { bool running, ticked; uint32_t tick, next_us; };
struct RandomSource { void* ctx; uint32_t (*next)(void*); };
struct ProcessContext {
  TimeSample now;
  const GlobalState& global;
  TransportView transport;
  RandomSource& random;
};
enum class Operation : uint8_t { Next, Add, OutNext, OutSource, Skip };
enum class ResetReason : uint8_t { Init, Stop, SceneChange, ParameterChange, Panic };
struct ParamSpec; struct ParamValue; struct ModuleState; struct AttributeVisitor;
struct ModuleDescriptor {
  const char* id; uint16_t version;
  const ParamSpec* params; uint8_t param_count;
  void (*construct)(ModuleState&);
  void (*destroy)(ModuleState&);
  void (*process)(ModuleState&, const ProcessContext&, const NoteBatch&, NoteBatch&);
  void (*reset)(ModuleState&, ResetReason);
  bool (*set_param)(ModuleState&, uint8_t, ParamValue, const GlobalState&);
  void (*attributes)(ModuleState&, AttributeVisitor&);
};
}
```

`FixedBuffer` duplicate identity is specialized by the owning operation, not guessed globally: raw input duplicates, scheduled duplicates, and held `(pitch,channel)` have different semantics. T01 must provide separate append and unique-insert helpers if needed. Off velocity is normalized explicitly at MIDI serialization. Preserve Python per-module multiplicity/order and note-array capacity/eviction semantics through source fixtures, while output safety overrides lost releases. A dropped note-on must not become a held note.

The input module contract uses `InputFrame` after shortcuts are consumed and produces `NoteBatch`; it additionally returns twelve pixel colors and bounded pad labels through an `InputView`. Input algorithms never call the hardware adapter. Define a distinct input descriptor and state union; do not force key/UI inputs through processor note-only APIs.

`ParamSpec` includes stable persisted name, type (int/fixed decimal/enum/action), default, inclusive range, step, display label, options span, and flags for persistence/UI-only. `ParamValue` is a tagged scalar; callbacks use typed setters and effect flags (`ReleaseNotes`, `Repaint`, `ResetModule`), handled by controller before next processing pass. Avoid function pointers capturing hardware. Persisted attribute descriptors specify name, scalar type, fixed element count and access; no general object serializer. Source fractional parameters must retain sufficient precision for exact text round-trip; choose float where source is float and prove acceptable numeric tolerance in tests.

The materialized `ParamSpec::global_binding` identifies OCT/PADOFS controls that request an authoritative global mutation. Input setters validate the tagged value and return release/repaint effects while retaining a const global view; core 0's controller applies the requested value to `GlobalState` before the next process call. This avoids per-input shadow globals and keeps renderer and input algorithms from mutating controller-owned state.

`Chain::process(ctx,input,out)` owns six slot states, immutable per-pass snapshots and scratch output. Each slot selects raw input/earlier source as Python does; disallow future taps. NEXT replaces stream; ADD merges original and processed; OUT+N sends processed to output and continues; OUT+S sends processed and continues original; SKIP bypasses. Source fixtures must establish exact duplicate and channel inheritance semantics. Output submission is explicit through a bounded `OutputCollector` owned by Chain, not a global.

`Transport::update(now,rx)` consumes a bounded input view and returns a snapshot plus ordered realtime events. `start/stop/continue` follow source reset/count semantics. `Output` is a single ordered FIFO (`kQueueCapacity` 48 packets, consecutive clocks run-length merged) plus two per-(channel,pitch) bitmaps: `sounding` (note-on accepted by the port, no note-off accepted since) and `off_owed` (release requested but not yet accepted). `submit` queues; `flush` sends owed releases first, then the FIFO strictly in arrival order, and advances state only on accepted sends. A note-off that does not fit the FIFO becomes owed (counted in `owed_deferred`), never dropped; a refused note-on/clock/start is counted (`queue_overflow`) and never becomes sounding. `all_notes_off(channel|-1)` erases queued ons/offs and owes exactly the sounding identities, so it is exact and safe at any time. Accessors: `held_count()` (uint16, sounding identities, bounded only by 16x128), `owed_count()`, `is_sounding`, `is_off_owed`, `pending()`, `diagnostics()` (refused, queue_overflow, owed_deferred, recoveries, disconnected). **Overflow rule:** `FixedBuffer` carries a sticky `overflowed` flag (set by any append refused as Full, cleared by `clear()` or reassignment; `NoteBatch::overflowed()` ORs both buffers). Modules may ignore `append` results, but `Chain::process` checks the input batch, every processor output and every ADD merge after each slot; any overflow (off or on) counts in `ChainDiagnostics::batch_overflow`/`scratch_overflow`, sets `recovery_required()`, and faults the `OutputCollector`, whose drain performs `Output::all_notes_off()` plus Stop (exact via the sounding bitmap, so releases collected in the lost pass are covered) before the controller resets the chain. A pass is never silently truncated. Ordered pending notes: a nonblocking `MidiPort::try_send(packet)` returning false retains the unsent packet. State advances only after accepted output. A fake port can refuse any send. USB MIDI event packet details remain in the adapter; domain events have kind, channel, data1, data2. No callback recursively invokes chain processing.

Platform ports use small structs of function pointers plus context (or concrete adapters injected through templates where simpler): `ClockPort::sample`, `InputPort::poll`, `MidiPort::receive/try_send/connected`, `DisplayPort::present_chunk/sleep`, `PixelPort::present`, and `StoragePort::read/write/close/rename/remove`. All return status; counts/lengths are explicit. Filesystem ports are used only outside performance mode. Host tests provide fakes; no mocked Arduino environment needed.

`SceneCodec::decode(reader,SceneDto&) -> SceneResult` never touches live modules. `SceneResult` distinguishes missing, malformed, unsupported, too-large and I/O failure. DTO holds globals, module IDs, typed parameters and declared attributes with compile-time registry-derived bounds. T01/T07 derive max line/scene size from all supported schemas and fixtures, add explicit headroom, document constants, and reject excess without partial mutation. `encode(writer,const SceneDto&)` writes source-compatible SCN1. Do not keep multiple rendered text scenes in RAM. `SceneService` owns staging and transaction phases; controller owns requests and status display.

`UiModel` is bounded copied data for the current screen, four parameter rows, scene picker/focus and pad indicators. It contains values and immutable table IDs, never live module pointers. Core 0 assembles only these bounded values; core 1 formats text, computes layout and renders into one framebuffer. Changes cannot invoke music callbacks during rendering. Constants/assets/music tables are generated into const arrays with a repeatable converter and provenance; preserve existing bitmap/font geometry on the 128x64 screen.

## Cross-core bridge (user-requested isolation)
```cpp
enum class InputKind : uint8_t { KeyEdge, EncoderSwitch, EncoderDelta, Resync };
struct PhysicalEvent {
  uint32_t sequence, epoch, at_ms;
  InputKind kind;
  uint8_t key;
  int16_t value; // down/up, delta, or current 12-bit key mask by kind
};
struct UiSnapshot {
  uint32_t revision, epoch;
  UiModel model; // fixed-size value type, including brightness and pixel colors
};
// Single producer/consumer per direction; try operations never wait.
// Core 1 -> core 0: 64 PhysicalEvent records, ordered edges and encoder deltas.
// Core 0 -> core 1: 3 UiSnapshot records, skip publish when full.
```

Only core 0 classifies shortcuts and musical edges, eliminating command-versus-key reordering. Do not implement a second independent UI command queue. Consume events sequentially; a switch edge precedes subsequent key shortcuts. Maintain a physical-state snapshot/fault latch outside the edge queue for overflow recovery; publish with the same safe memory ordering. At scene transition increment epoch, discard stale physical events, require release/resync before generating new notes, and publish the new epoch. Old held keys must not generate notes in a different scene.

SPSC head is producer-owned, tail consumer-owned. Write payload before release-store of head; acquire-load head before reading. Reader releases tail only after copying payload locally. Producer acquire-loads tail before reusing storage. Use aligned 32-bit atomic loads/stores; verify target generated instructions and boundedness in T11a. Do not assume Cortex-M0+ read-modify-write atomics are lock-free. No `volatile`-only synchronization, mutex waits, raw hardware FIFO use, or unsafe double-buffer overwrite. If the toolchain cannot support bounded atomic load/store, Sol may use a documented SDK nonblocking try primitive with explicit failure handling and equivalent publication semantics; record evidence, escalate if no bounded strategy works. Snapshot consumer drains at most three entries, copies newest into core-local memory, then renders.

Startup uses a published readiness flag; neither core waits in a spin loop for setup. Maintenance uses request/ack sequence flags: core 0 stops/release-drains, requests core 1 quiescence, and continues USB polling until acknowledgement. Core 1 finishes its bounded transfer, acknowledges, then avoids peripherals until resume. Only then may core 0 write flash. A timeout cancels the transaction with an error, never forcibly blocks a running core. Resume increments input epoch and performs physical resync. Library-internal flash pauses remain a measured maintenance limitation.
