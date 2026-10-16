# Adding a module to the C++ firmware

This guide describes the pattern the existing modules follow (`firmware/Orbit12/src/modules/core/`, T09a to T09j; the sources moved there from `domain/modules/`). The Python source in `circuitpython-source/src/apps/Orion/` is the behavioral oracle; `behavior-contracts.md` indexes each module's source-backed defaults and ranges. Read `AGENTS.md` first for the platform rules (no heap in the performance loop, fixed capacities, zero-based channels, no exceptions or RTTI).

Two kinds of module exist:

- **Processors** (chain slots 1 to 6) use `ModuleDescriptor`: they receive a `NoteBatch`, emit a `NoteBatch`, and have `ModuleState` storage.
- **Input modules** (the six input slots: note, chords, drum, matrix, note_walk, chord_walk) use `InputDescriptor`: they receive key events and write pad colors and labels. Section 7 covers them.

Use `quantize_strum` and `wander` as the reference pair for processors. Use `chords_input` and `inputs.cpp` for inputs.

## 1. Files to touch (processor)

| Step | File | Change |
|---|---|---|
| 1 | `modules/core/<group>/<name>.hpp/.cpp` (or `modules/user/<author>/<name>/` for third-party code) | Declare the construct, process, reset, set_param, attributes and descriptor accessor functions. The `.cpp` file header lists every deviation from the Python source, numbered, with the reason. |
| 2 | `modules/common/module_state.hpp` (core) or `modules/user/<author>/<name>/<name>_state.hpp` (user) | Add a `<Name>ModuleState` struct with default member initializers. The union member is generated from `module.json` (`state.type`, and `state_header` for user code). |
| 3 | `firmware/Orbit12/src/modules/core/<id>/module.json` | Write the manifest (fields in `docs/modules/README.md`). Run `python -I tools/modules/gen_registry.py`; it regenerates the registry, descriptor table, module inventory and scene bounds under `modules/generated/`. Do not hand-edit the generated files; `--check` fails when they are stale. |
| 4 | `tests/native/CMakeLists.txt` | Add `orbit_native_test(...)` for the new test and list every module `.cpp` it needs (see step 6). Tests that link the registry also link `modules/generated/registry_generated.cpp`. |
| 5 | Earlier tests | Bump every `module_count() == N` assertion. Current tests that assert it: `t06_chain`, `t09b` through `t09j`, and `t16_registry_equality`. |
| 6 | `tests/native/fixtures/<id>/` | Add `gen_traces.py` and `shims/adafruit_ticks.py` (copy from `t09j`). Run the generator; commit its output `tests/native/fixtures/<id>_source_traces.inc`. |
| 7 | `tests/native/<id>_<name>.cpp` | Include the `.inc` inside the test's anonymous namespace, run its traces, and add invariant tests. |

Registry entries, scene keys, footer descriptions, UI selector tables, the scene input layout and the scene bounds are generated from `module.json` (`tools/modules/gen_registry.py`, plan item E01 done for these tables). The state union is generated from `module.json` (`modules/generated/module_state_generated.hpp`); a core state struct is still written in `modules/common/module_state.hpp`. Chain code and scene persistence look modules up through the registry, so they need no per-module change.

Hidden entries stay hidden. `phraser` has no implementation; `find_module("phraser")` must return `nullptr` (checked in the tests).

## 2. State

Put every runtime field in a state struct in `modules/common/module_state.hpp`:

- Use fixed arrays and `NoteBuffer` (`FixedBuffer<Note, kPolyphony>`) for held notes and scheduled items. `NoteBuffer` carries a sticky `overflowed` flag and `count`.
- Give fields the source's default values through default member initializers. `construct_*` uses placement new (`new (&state.storage.<name>) <Name>ModuleState{}`).
- Keep the union small. `ModuleStorage` (generated) is as large as its largest member (632 bytes at T09j, set by an existing module; `WanderModuleState` is 144 bytes and does not change it). The chain asserts `sizeof(ModuleState) * 6 <= 128 KiB` (`chain.hpp`). Report the new size in the task report.
- Keep the transport tick as the unwrapped 64-bit value, not the raw `uint32`. The source's `midi_tick` is unbounded, and grid bins, step counters and seeds depend on it. `quantize_strum` and `wander` track `tick_known`, `last_tick` and `tick` and unwrap 32-bit deltas.

## 3. Descriptor and parameters

```cpp
const ModuleDescriptor kWanderDescriptor{"wander", 1, kWanderParams, 11,
    construct_wander_state, descriptor_destroy, process_wander, reset_wander_state,
    set_wander_param, wander_attributes};
```

- `id` is the source module name. `version` matches the source `version`.
- `params` is in the order of the source's `create_main_parms()` (the module's own controls only). Input and output parameters (`OP`, `OUTCH`, `SRC`) belong to the chain.
- Each `ParamSpec` has the source parameter name, type, default, minimum and maximum (from the source's `minmax`), step (the source's `increment` or 1), display label (from the source), options (for enumerations), and flags.
- `ParamPersist` makes the value a scene field. `ParamUiOnly` and `Action` types are never persisted (`persistence/scene_codec.cpp`, `field_persisted`). `arp_walk` uses this for its RAND button.
- `set_param` validates the typed value. Source parameters with `minmax` clamp (`Parm.set_value`), so the C++ clamps too. Parameters without `minmax` that the source uses as table indexes are rejected. Decide which applies from the source and say so in the `.cpp` header.
- Reject non-finite floats before any cast. Clamp floats before casting to an integer.
- `effects` is `EffectNone` unless the source callback changes state that needs release or reset. Output and transport are never touched from `set_param`.
- Saved attributes (source `save_attrs`) use `AttributeVisitor`: `arp_walk_attributes` visits `step_values`. A module with no attributes implements an empty visitor, as `wander` does.

## 4. Process rules

- `process(state, context, input, output)` clears `output` first (`output = {}`).
- Timing: `context.now.ms` is a wrap-safe `uint32`. A deadline is due when `static_cast<std::int32_t>(deadline - now) < 0` (strictly before now), which matches the source's `ticks_less(due, now)`. `domain/time.hpp` has `deadline_reached`, which is non-strict (`>=`); use `!deadline_reached(due, now)` for the strict form. Deadlines must be less than 2^31 ahead.
- Transport: `context.transport.running` and `tick`. Keep `was_running` and `last_grid_bin` in state. The restart rules follow the source's `process()` order (`wander` lists them in comments).
- Call order matches the source: a note-on that starts a held set may set `last_grid_bin` to the current bin so that it does not sound on that same pass.
- Random: source hashes are exact in `seeded::random_int` and `seeded::hash_u32` (`drop_randomize.hpp`). Use them rather than `ProcessContext.random` when the source is deterministic, so traces need no injection.
- Channels: domain channels are zero-based. The chain applies the slot's output channel (`Chain::configure`, `OutputCollector::append`), so a module may emit channel 0 when the source has no channel concept. A release must match its note-on's pitch and channel (store the sounding `Note`).
- No logging, no heap, no blocking, no exceptions. Diagnostics are counters in state (`held_overflow`, `output_overflow`, and so on).

## 5. Release safety

The rules below come from the existing modules. Each one is a documented decision.

1. **Never lose a release.** A sounding note is recorded in state (`has_active` and `active`). Its release is emitted through `output.off` when due, or when the held set empties.
2. **Check capacity before emitting a pair.** A note-on and its previous release are emitted together, only when both fit (`wander` W4, `arp_walk` deviation 1). If they do not fit, leave the previous note sounding, count `output_overflow`, and let the sticky flag surface.
3. **Sticky overflow is a fault.** `FixedBuffer::append` on a full buffer sets `overflowed` and returns `Full`. `Chain::process` treats any `overflowed()` batch as a fault: it calls `OutputCollector::fault()`, which triggers `all_notes_off` and `queue_stop` when drained (`chain.cpp`, `OutputCollector::drain`). Do not clear the flag or drop items silently.
4. **Stop, Panic, SceneChange, Init.** `Chain::reset` only calls each descriptor's `reset`. It does not release notes. A module that drops a sounding note on Stop (as the source's `stop()` does) must say so, and the controller must call `Output::all_notes_off` on those transitions. Current status: no controller exists in `firmware/` yet (plan T11b and T14). Treat this as a requirement until it is wired.
5. **Parameter edits do not reset** unless the source resets (`ResetReason::ParameterChange` is a no-op in `wander`, `quantize` and `strum`).
6. **Duplicates.** `OutputCollector` collapses exact duplicate events within one call only. Do not rely on it for correctness.

## 6. Tests and fixtures

Each module has three kinds of checks:

- **Source traces** (`fixtures/<id>/gen_traces.py`, output `fixtures/<id>_source_traces.inc`). The generator imports the Python module from `circuitpython-source/src` under `python -I`, with `adafruit_ticks` replaced by `shims/adafruit_ticks.py` (2^29 ms wrap). It runs each trace through the Python class and writes every step as `r.step(...)` with the Python output as the expected value. Generated files are never edited by hand. Regenerate with `python -I tests/native/fixtures/<id>/gen_traces.py` from the repository root.
- **Deviation traces** (listed in the generator, as `DEVIATION_TRACES` in `t09j` or `DEVIATIONS` in `t09i`). A deviation must differ from the source. The `t09j` generator simulates the source with the transport running at construction and the C++ behavior with it stopped, then writes `r.step_dev(...)` for each step where they differ. It fails if a listed trace has no difference.
- **Invariants** (randomized and boundary tests) that do not reuse the implementation: monophony, release correspondence, gate deadlines (strict, across the uint32 wrap), reset, bounds and held overflow.

Test shape: copy the `Run` harness from `t09j_wander.cpp` (construct, `set_int`, `set_float`, `set_global`, `stop`, `process`, `step`, `step_dev`). Include the generated `.inc` inside the anonymous namespace. Include `domain/modules/registry.hpp` and the new header.

Commands (Windows, repository root):

```
powershell -NoProfile -File tools/arduino/test-native.ps1 -BuildDirectory build/<id> -Configuration Release
```

The build treats warnings as errors and CMake strips `NDEBUG`, so `assert` checks stay in Release. Record tests actually run (the CTest line) separately from hardware checks, which are pending.

Note: `tests/native/README.md` describes JSONL fixtures. The modules from T09a onward use generated `.inc` traces instead. The README needs reconciling; this guide follows the modules.

## 7. Input modules

- Add a `<Name>InputState` struct, a member in the generated `InputStorage` union (the member comes from `module.json`), and `construct/process/reset/set_param/attributes` functions.
- `process` receives an `InputFrame` (key events with `at_ms`, encoder deltas and button edges), writes a `NoteBatch`, and writes `InputView` pad colors and labels. Labels must point at static or module storage valid until the next call.
- Input descriptors are registered by the generator (`modules/generated/input_registry_generated.cpp`) from `module.json`. A user input is supported (ADR-12): it gets a scene bank in every scene file, and the generator enforces the bounds (`docs/modules/README.md`).

## 8. Checklist

- [ ] Source read (`circuitpython-source/src/apps/Orion/modules/<name>.py`) and `behavior-contracts.md` entry checked; discrepancies recorded.
- [ ] Deviations listed in the `.cpp` header, each with its reason.
- [ ] State struct written (`state_header` for user code); the generator's `sizeof` check passes.
- [ ] `module.json` written; `gen_registry.py --check` passes; the registry and descriptor tables are generated, not edited.
- [ ] Parameter order, defaults, ranges, clamp or reject, flags and labels match the source.
- [ ] Every note-on has a tracked release; capacity checked before emitting a pair; `overflowed()` never cleared.
- [ ] Stop and reset behavior matches the source; the controller `all_notes_off` requirement is noted.
- [ ] Generated traces and deviation traces produced by `python -I`; `.inc` not edited.
- [ ] Invariant tests added (monophony, release, gate strictness and wrap, reset, held overflow).
- [ ] CMake: new test target, `target_sources` for every earlier registry-linked test.
- [ ] Release build passes; the test binary reports `sizeof(ModuleState)`.
