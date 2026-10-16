# Runtime migration discovery

Scope: timing, concurrency, bounded state, and deployable data/assets for an Arduino migration. This is a source inventory, not a proposed redesign. No repository `AGENTS.md` applied when this discovery note was prepared; the nearest discovered files were in unrelated sibling repositories.

## Runtime and timing evidence

- The shipped entry point is `circuitpython-source/src/code.py`; it opens the launch bitmap, waits 1.5 s, releases those references, then constructs `Orion` and enters `Orion.run()` indefinitely. Startup UI and run-loop semantics are therefore part of compatibility.
- Work is single-threaded and cooperative: each pass reads keys/encoder, updates transport, processes input plus six slots and output, flushes MIDI, does opportunistic GC, then handles queued UI and display refresh (`circuitpython-source/src/apps/Orion/Orion.py:574-846`). There are no threads, `async`, or RTOS task declarations in `circuitpython-source/src/`.
- Transport is millisecond based, with 24 MIDI-clock ticks per quarter note: interval = `60000/(BPM*24)` (`circuitpython-source/src/apps/Orion/core/transport.py:36-38`). At 120 BPM the nominal tick interval is 20.83 ms; at BPM `b`, it is 2500/b ms. Fractional milliseconds are accumulated, and swing changes tick spacing by up to ±60% (`MAX_SWING=0.6`, `transport.py:8-10, 44-53`).
- Internal mode advances at most one tick per `update()` call when late, then advances the scheduled deadline from the prior deadline (`transport.py:82-96, 109-114`). This avoids skipping clock count but can leave pending ticks after a stalled pass. Output drains all pending clock ticks in a `while` loop (`circuitpython-source/src/apps/Orion/core/output.py:62-68`); backlog drain is unbounded in this loop pass. UI callbacks and display refresh require 12 ms slack before the next internal tick (`circuitpython-source/src/apps/Orion/Orion.py:331-360, 835-844`); refresh cadence uses `run_tick % 4` and increments once per loop (`circuitpython-source/src/apps/Orion/Orion.py:844-851`).
- External clock and MIDI draining read at most 200 messages per transport pass (`transport.py:62-105`); external mode consumes one TimingClock and breaks, while prior messages such as Start/Stop may be consumed in that same pass. Preserve this input-buffer behavior or explicitly test changed buffering.
- The host loop has no explicit sleep/yield (`circuitpython-source/src/apps/Orion/Orion.py:574 onward`). Key scan interval is 3 ms, encoder switch debounce is 1 ms, and key event queue capacity is 64 (`circuitpython-source/src/apps/Orion/Orion.py:75-84`). These are scan/debounce settings, not guarantees of loop cadence or MIDI jitter.
- Screen sleeps after 60,000 ms inactivity (`circuitpython-source/src/apps/Orion/Orion.py:66, 373-380`). Internal-mode GC runs only when no pending clock tick, at least 12 ms from next tick, no queued UI work, and >300 ms since last collection (`circuitpython-source/src/apps/Orion/Orion.py:344-363`); transport start/stop also explicitly collects (`transport.py:124-137`). Runtime/GC pauses are observable timing behavior; CircuitPython docs characterize the app as non-hard-real-time (`circuitpython-source/docs/UX.md:19-26`).

## Bounded state and memory evidence

- Global limits: 10 chain slots, six processing slots, 12-note base polyphony, four parameters per UI page (`circuitpython-source/src/apps/Orion/core/constants.py:1-4`). The main note-on/off scratch arrays are base-sized; output note-on, note-off, and held-note arrays each have capacity `POLYPHONY*2 = 24` (`circuitpython-source/src/apps/Orion/Orion.py:97-98`, `circuitpython-source/src/apps/Orion/core/output.py:12-15`). Note arrays use bytearrays for notes/velocity/channel and `array('I')` for scheduled times (`circuitpython-source/src/apps/Orion/core/note_array.py:1-18`).
- Processing modules and input modules are lazy-loaded via registries; scene teardown clears references and calls GC. Scene switching deliberately saves then calls `supervisor.reload()` to reclaim the old module chain (`circuitpython-source/docs/Architecture.md:39, 77-89`, `circuitpython-source/src/apps/Orion/Orion.py:220-250, 295-324`). A native port needs an explicit RAM/flash budget and lifecycle equivalent; source does not state numeric free-RAM targets.
- Per-module state is not globally capped by one value. For example, `euclid.py` and `matrix.py` maintain fixed lane/pad arrays, while `bounce.py`, `delay.py`, `arp.py`, and `arp_walk.py` keep scheduled notes in note arrays. Inspect each module’s constructor and `NoteArray` max lengths before setting a global event bound.
- Display assets are six small BMPs duplicated from `circuitpython-source/src/apps/Orion/imgs/` into `circuitpython-source/src_mpy/apps/Orion/imgs/`; source asset total is 1,884 bytes (112+104+76+400+1,088+104). They are opened with `displayio.OnDiskBitmap`, so current code reads from filesystem rather than copying full pixel buffers into Python heap (`circuitpython-source/src/code.py:20`, `circuitpython-source/src/apps/Orion/core/ui.py:19-28`). `circuitpython-source/UI_Design/` is a separate design source with additional variants; runtime loads only assets in the Orion image directory.
- The project has two deployment trees: editable `circuitpython-source/src/` and compiled `circuitpython-source/src_mpy/`. Build tooling compiles only CircuitPython `.py` into `.mpy` and copies non-Python assets (`circuitpython-source/tools/build_mpy.py`); README says `circuitpython-source/src_mpy/` is the lower-memory board deployment. Arduino migration should identify its source-of-truth and packaging path explicitly rather than assume Python sources alone include the firmware payload.

## Data and compatibility inventory

- Settings and scenes are line-oriented text, not JSON at runtime despite checked-in files named `settings.json`: user settings path is `/userdata/Orbit12Orion/user_settings`, with `key_brightness` currently the only documented persisted setting (`circuitpython-source/src/apps/Orion/core/user_settings.py:1`, `circuitpython-source/docs/Architecture.md:72-75`). Scenes use `SCN1`, `S`, `M`, `P`, `A`, `END` records; ten slots `Scene01`–`Scene10`; active pointer `_activescene` (`circuitpython-source/docs/Architecture.md:60-70`). Inspect `circuitpython-source/src/apps/Orion/core/scenes.py` for escaping/version fallback details before defining a new serialized representation.
- `circuitpython-source/src/userdata/global_settings.json` and `circuitpython-source/src/userdata/Orbit12Orion/settings.json` are present but empty in this checkout (0 bytes); they are not the runtime persistence files described above. Treat this as packaging/repository state to clarify, not valid defaults.
- MIDI output sends NoteOffs before NoteOns each pass; held-note tracking suppresses duplicate note-ons, retriggers by ensuring off precedes on, and keeps channels per note (`circuitpython-source/src/apps/Orion/core/output.py:21-59, 103-117`). All-notes-off tracks channels, and output channels are zero-based internally while UI labels 1–16 (`circuitpython-source/docs/UX.md:52-62`).
- No `test/`, `tests/`, fixture, or hardware-simulation tree is present. Images are the only obvious static test/deploy assets. Migration acceptance coverage will need a separately agreed harness or hardware procedure.

## Acceptance checks implied by existing behavior

1. Verify internal MIDI clock at several BPM values against 24 PPQN and fractional interval accumulation; include swing extremes and a deliberately stalled loop to check late-tick catch-up and pending-clock drain behavior.
2. Verify external-clock message ordering and the 200-message per-pass bound, including Start/Stop/Continue and at least one TimingClock after queued control messages.
3. Exercise note-on/off ordering, duplicate suppression, same-note retrigger, channel isolation, and all-notes-off with maximum 24 output/held entries; check behavior when fixed arrays fill.
4. Round-trip all ten scenes and active-scene selection, including every registered module’s persisted parms/attrs, missing/unknown module fallback, and malformed/older scene records.
5. Verify all six runtime BMP paths, dimensions/pixel appearance on target display, launch-screen delay, deferred refresh, four-loop refresh cadence, and 60 s screen sleep/wake.
6. Record steady-state and peak RAM/flash on the actual Arduino target with the largest supported module chain and scene load. Existing code provides no numeric memory threshold to inherit.

## Unknowns to resolve from the target

- Arduino board, framework, MIDI transport implementation, display driver, and whether filesystem assets remain available.
- Required timing tolerance and load envelope; current docs claim musical/useful timing but do not specify jitter bounds.
- Which CircuitPython libraries/board behaviors have direct target equivalents, especially `adafruit_ticks`, `keypad`, `adafruit_macropad`, `displayio`, USB MIDI, and `supervisor.reload()`.
- The exact meaning of checked-in empty `settings.json` files and whether they are stale artifacts.

## Module storage and scene payload bounds

Every `NoteArray`, `NoteOnArray`, and `NoteOffArray` defaults to 12 entries (`circuitpython-source/src/apps/Orion/core/note_array.py:12-18, 211-217`). In this table `12×N` means N independent arrays with capacity 12; `6×2` means two arrays with capacity six. Modules also keep regular scalar state and, where noted, fixed-size byte arrays. Parameter counts are main-parameter count plus inherited controls: processors add `SRC`, `OP`, `OUTCH` (+3); inputs add `OP`, and musical inputs add `OCT`, `PADOFS`, `VEL` (+3). Counts include generated controls expanded to runtime quantity.

| Module | Note arrays (capacity each) | Main parameters | Saved attributes and fixed payload |
| --- | --- | ---: | --- |
| empty | none | 0 + 3 common | none |
| arp | 6×12 | 12 | none |
| arp_walk | 6×12 | 13 | `step_values`: 8 entries |
| bounce | 6×12 | 9 | none |
| chord | 2×12 | 2 | none |
| chopper | 4×12 | 5 | none |
| delay | 6×12 | 2 | none |
| drop | 2×12 | 4 | none |
| euclid | 2×12, 2×6 | 8 | `pulses`, `lengths`, `rotations`: 6 entries each (18); active note and time arrays: 6 each |
| gate | 4×12 | 3 | none |
| latch | 3×12 | 1 | none |
| pick | 5×12 | 5 | none |
| quantize | 7×12 | 1 | none |
| randomize | 2×12 | 12 | none |
| retrigger | 3×12 | 2 | none |
| strum | 3×12 | 5 | none |
| transpose | 2×12 | 3 | none |
| wander | 3×12 | 11 | none |

For inputs, actual total controls are: `note` 5, `drum` 5, `matrix` 17, `chords` 12, `note_walk` 4, and `chord_walk` 10. Matrix's 13 main controls are selector, eight pad values, bass, spread, and reset. ChordWalk has six main controls: selector, four pad values, and preview. Counts can be checked in each module's `create_main_parms`, with inheritance in `circuitpython-source/src/apps/Orion/core/module.py:58-111` and `circuitpython-source/src/apps/Orion/core/_modules/_input.py:15-54`. Arp is the widest implemented module at 15 total visible parms; ArpWalk totals 16. The UI displays four rows per page (`circuitpython-source/src/apps/Orion/core/constants.py:4`).

The global note-on/off scratch buffers hold 12 events each. Output note-on, note-off, and held-note buffers each hold 24 (`circuitpython-source/src/apps/Orion/Orion.py:110-111`, `circuitpython-source/src/apps/Orion/core/output.py:12-15`). Generic append returns `False` at capacity (`circuitpython-source/src/apps/Orion/core/note_array.py:158-170`), but many callers ignore that result, so full scheduled/module arrays can silently drop events. Output queue full behavior is mixed: queue overflow for a NoteOff sends it immediately, but held-note appends and NoteOn queue appends do not check failure (`circuitpython-source/src/apps/Orion/core/output.py:21-59`). `pop_due` likewise ignores output append failure (`circuitpython-source/src/apps/Orion/core/note_array.py:181-194`). The fixed capacity controls memory but does not guarantee lossless overload behavior.

Scene files do not impose a byte ceiling. They emit `SCN1` plus seven global records, at most ten module records, and persisted non-button parameters/attrs as decimal text; array attrs use `|` delimiters (`circuitpython-source/src/apps/Orion/core/scenes.py:88-105, 131-177`). The largest bounded attr is a 12-element Matrix pad array; Matrix saves eight arrays (96 scalar values), Euclid saves 18 values, ChordWalk 16, and ArpWalk 8. Exact maximum encoded bytes cannot be established from source: some parameter values have no `minmax`, attributes restore without length/range validation, and float string length is runtime dependent. Scene parser splits record commas without escaping (`circuitpython-source/src/apps/Orion/core/scenes.py:240-307`); current attrs are numeric arrays, not arbitrary text.

The processor registry advertises `phraser` but has no import mapping or class dispatch, so it resolves to `None` and falls back at load (`circuitpython-source/src/apps/Orion/modules/_registry.py:5-26, 39-95`). There are 17 implemented musical processors, plus the empty slot filler, despite 18 advertised names.

## Transport range and wrap behavior

The UI exposes BPM 1–300 and swing 0–1 (`circuitpython-source/src/apps/Orion/core/_modules/_set.py:20-30`; duplicate controls in `_transport.py:30-42`). This gives nominal tick intervals of 2500 ms at 1 BPM and 8.333 ms at 300 BPM. Full swing multiplies alternating intervals by 1.6 and 0.4, giving a 4,000/1,000 ms pair at 1 BPM and approximately 13.333/3.333 ms at 300 BPM. Scene files directly assign BPM without clamping before `update_bpm`, so those UI bounds are not input validation for malformed data (`circuitpython-source/src/apps/Orion/core/scenes.py:263-276`).

Clock comparisons use `adafruit_ticks.ticks_add`, `ticks_diff`, and `ticks_less` (`circuitpython-source/src/apps/Orion/core/transport.py`, `circuitpython-source/src/apps/Orion/Orion.py`, `circuitpython-source/src/apps/Orion/core/note_array.py`), which are intended to make deadline comparisons safe across counter rollover as long as compared times are inside the helper's signed-difference horizon. The helper is checked in only as `circuitpython-source/src/lib/adafruit_ticks.mpy`; its exact period and half-period cannot be confirmed from this checkout. Scheduled note deadlines are held in `array('I')`; preserve its unsigned width and rollover together with the helper semantics. The source's observable assumption is wrap-aware comparisons, not a specific documented millis period.

### Input-module storage

Input modules also allocate the base 12-entry note-on and note-off outputs. Additional storage and saved data:

| Input module | Additional note/relationship arrays | Main controls | Saved attrs / bounded payload |
| --- | --- | ---: | --- |
| note | relationship 12 | 1 | none |
| chords | note arrays 3×6, relationship 12 | 8 | none |
| drum | relationship 12 | 4 | none |
| matrix | note array 1×6, relationship 36 | 13 | eight attrs × 12 values = 96 scalars |
| note_walk | note array 1×12 (velocity) | 0 | none |
| chord_walk | note arrays capacities 4, 8 (velocity), 6 | 6 | four attrs × 4 values = 16 scalars |

`NoteRelationshipArray` stores three bytearrays per entry for input/output/return note tracking (`circuitpython-source/src/apps/Orion/core/note_array.py:221-230`), so capacities 12 and 36 imply respectively 36 and 108 bytes of raw relationship payload, excluding Python object/allocator overhead. The table and processor counts describe item capacity/counts. A firm byte maximum for total scene text is unavailable because parameter values may lack bounds and floats are formatted dynamically.
