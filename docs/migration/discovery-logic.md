# CircuitPython to Arduino discovery: Orion logic

Scope: behavior that an Arduino port must preserve. The checked-in readable implementation is `circuitpython-source/src/`; `circuitpython-source/src_mpy/` contains compiled `.mpy` deployment files and was not used as the logic source. The repository had no `AGENTS.md` when this discovery note was prepared.

## Runtime and module inventory

- `circuitpython-source/src/code.py:10-56` is the board entry point: disable display auto-refresh, show the launch bitmap, clear its allocations, import Orion, construct it, and enter `run()`.
- `circuitpython-source/src/apps/Orion/Orion.py:77-144` initializes MacroPad hardware and managers, creates the shared `ModuleHelper`, wires cross references, loads user settings, creates fixed slots, restores the active scene, fills absent chain slots, and builds the UI.
- Chain slot IDs and defaults are in `circuitpython-source/src/apps/Orion/core/state.py:205-217` and `circuitpython-source/src/apps/Orion/Orion.py:147-161`: SET=0, IN=1, six processing slots=2..7, OUT=8, SETTINGS=9. Stock processing slots are `empty`; set/input/output/settings are fixed system modules.
- Input modules are `note`, `chords`, `drum`, `matrix`, `note_walk`, and `chord_walk` (`circuitpython-source/src/apps/Orion/inputmodules/_registry.py:3-17`). Processing registry contains empty, arp/arp_walk, bounce, chord, chopper, delay, drop, euclid, gate, latch, pick, quantize, randomize, retrigger, strum, transpose, wander (`circuitpython-source/src/apps/Orion/modules/_registry.py:3-24`). `phraser` is listed but has no source/import handler, so it cannot currently load.
- Module imports are lazy and unload hooks remove imported class modules for memory (`circuitpython-source/src/apps/Orion/Orion.py:45-57`; registries); constructor `MemoryError` falls back to Note for input or Empty for processor (`circuitpython-source/src/apps/Orion/core/state.py:76-110`). Arduino memory strategy will differ, while visible selection/load fallback behavior is user-facing.

## State and note flow

- Defaults: key C, scale index 1, octave 3, pad offset 0, 120 BPM, swing 0, internal transport (`circuitpython-source/src/apps/Orion/core/state.py:38-48`). `TimingSteps` is present but not included in the scene's saved global attributes.
- `ModuleHelper` is the modules' shared access bundle for state, hardware, input/output managers, UI, pixels, and transport (`circuitpython-source/src/apps/Orion/core/module.py:37-48`). Modules expose lazily built parameter lists; common controls include operation (`NEXT/ADD/OUT+N/OUT+S/SKIP`), source tap, and output channel (`module.py:9-27, 82-130`).
- `Orion.run()` polls encoder and key state, translates unconsumed key presses/releases to note-on/off arrays, updates transport, runs IN then all six processing slots then OUT, flushes MIDI notes/clock, and services deferred UI (`circuitpython-source/src/apps/Orion/Orion.py:574-585, 622-644, 798-851`). Preserve this order: modules can schedule events during the current pass and output flush follows the entire chain.
- Inputs are 12 keys plus encoder switch; key indices are shifted to 0..11 when making note events (`circuitpython-source/src/apps/Orion/core/input.py:10-35`; `circuitpython-source/src/apps/Orion/Orion.py:804-809`). Encoder turn is sampled every eighth poll (`input.py:16-19, 39-64`). Holding encoder changes keys into shortcuts; e.g. encoder+key 2 starts clock, +key 1 stops (`circuitpython-source/src/apps/Orion/Orion.py:606-644`). Exact UI shortcut map is in the rest of `circuitpython-source/src/apps/Orion/Orion.py:622-797`.
- Input base maps physical pad order via `PADMAP=(9,10,11,6,7,8,3,4,5,0,1,2)` and converts scale degrees with global key/scale/octave/offset (`circuitpython-source/src/apps/Orion/core/_modules/_input.py:7-10, 111-124`). Set slot owns global key, scale, BPM, swing, transport mode (`circuitpython-source/src/apps/Orion/core/_modules/_set.py:18-33`). Key/scale/octave/offset edits clear held MIDI notes; key/scale changes repaint UI/pixels (`_set.py:40-53`; input callbacks `_input.py:66-85`).
- `Module.process_super()` applies source routing and operation behavior around each module's `process()` (`circuitpython-source/src/apps/Orion/core/module.py:178-219`). Source taps can select raw input or an earlier slot. NEXT passes output onward; ADD merges original and processed notes; OUT+N sends then continues; OUT+S sends and returns the original stream; SKIP bypasses processing. Arduino compatibility should explicitly match these distinctions.
- Note containers are fixed-capacity byte arrays (default polyphony 12); note-on arrays include velocity, scheduled/output arrays can additionally carry channels (`circuitpython-source/src/apps/Orion/core/constants.py:1-6`, `circuitpython-source/src/apps/Orion/core/note_array.py:10-29, 151-161`). MIDI output de-duplicates queued note-ons, sends note-offs before note-ons, tracks held notes by channel, and supports all-notes-off (`circuitpython-source/src/apps/Orion/core/output.py:12-47, 65-103`).

## MIDI and transport behavior

- Hardware wrapper uses USB MIDI port 0 input / port 1 output, defaults to MIDI channel 1 (zero-based conversion), and only supports rotation 0 (`circuitpython-source/src/apps/Orion/core/orion_macropad.py:19-45, 47-65`). It returns `midi=None` if ports are unavailable; callers otherwise assume MIDI exists, so disconnected-MIDI handling is an uncertainty/port concern.
- Internal clock interval is 60,000/(BPM*24) ms; fractional remainder is accumulated, and swing changes tick interval in pairs (six ticks per half, max factor ±0.6) (`circuitpython-source/src/apps/Orion/core/transport.py:7-10, 23-39, 59-73`). Clock start resets timing, schedules next tick, marks running, then emits MIDI Start plus TimingClock; stop resets, emits Stop, stops modules and clears queued clock ticks (`transport.py:93-110`).
- External mode reads clock/start/stop/continue from MIDI input, starts at first TimingClock, increments on each clock, and consumes at most one TimingClock per update. Internal mode drains incoming MIDI messages without handling them (`transport.py:41-57`). Internal ticks are queued and flushed after note processing (`circuitpython-source/src/apps/Orion/Orion.py:811-827`).
- UI and garbage collection are delayed near clock deadlines (`circuitpython-source/src/apps/Orion/Orion.py:332-367, 831-849`); port should keep display work from blocking clock/note deadlines.

## Persistence and lifecycle

- Scene files live at `/userdata/Orbit12Orion/scenes/Scene01`..`Scene10`; active scene is 1-based in `_activescene` (`circuitpython-source/src/apps/Orion/core/scenes.py:8-11, 44-80`). Format is text `SCN1`, `S,attr,value` globals, `M,slot,module,version`, `P,param,value`, optional `A,attr,value`, `END` (`scenes.py:141-210`).
- Persisted global fields: key, scale, octave, key_offset, BPM, swing, transport_mode (`scenes.py:15-23`). For each module it writes parameter values except button/UI-only parameters and serializes declared `save_attrs` (`scenes.py:131-181`). Load recreates modules and applies parameter edit/exit callbacks (`scenes.py:240-303`). Module version is written but not consulted on load. Scene module records also remap legacy SET/transport slot layouts (`scenes.py:184-189`).
- Missing scene returns `None`; invalid header or load memory failure returns `False`; startup handles missing by restoring/saving defaults and invalid by resetting scene (`circuitpython-source/src/apps/Orion/Orion.py:249-269`). A scene switch saves current state (or current state into target when copy requested), changes active pointer, saves user settings, and calls `supervisor.reload()` to rebuild RAM (`circuitpython-source/src/apps/Orion/Orion.py:287-327`). Reset unloads modules, restores defaults and stock chain, clears note arrays (`circuitpython-source/src/apps/Orion/Orion.py:147-181`).
- User setting file is `/userdata/Orbit12Orion/user_settings`, currently only `key_brightness` in range 0..1 (`circuitpython-source/src/apps/Orion/core/user_settings.py:1,16-46`). The checked-in `circuitpython-source/src/userdata/global_settings.json` and `circuitpython-source/src/userdata/Orbit12Orion/settings.json` are not read by these functions; relationship to deployment/config tooling is unclear.

## Porting uncertainties / decisions to resolve

1. Target board, display, keypad/encoder, LED, and USB-MIDI libraries are unspecified. Hardware abstraction is required; current code is tied to Adafruit MacroPad/CircuitPython APIs.
2. Current implementation assumes a filesystem rooted at `/userdata` with directories already provisioned; scene save does not create the scene directory. Confirm Arduino storage layout and migration/import expectations.
3. Legacy scene parser ignores recorded module version and relies on parameter names and types. Decide whether to preserve this text format exactly or add an explicit migration layer; ensure existing user scenes remain importable.
4. `circuitpython-source/src/userdata/*.json` appears separate from runtime scene/settings persistence; inspect build/sync tooling before deciding whether these files are source defaults or deployment artifacts.
5. `phraser` appears as a selectable registry entry without implementation; remove/hide or restore it before compatibility claims.
6. The old runtime sets CPU frequency to 250 MHz (`circuitpython-source/src/apps/Orion/Orion.py:19`); target platform may not permit or need this. It should not be translated blindly.

## Compatibility contract for the Arduino port

| Record | Current encoding | Load/save behavior |
|---|---|---|
| Header | `SCN1` | First line required; any other value rejects the scene. |
| Global | `S,<attribute>,<value>` | Attributes: `key`, `scale`, `octave`, `key_offset`, `bpm`, `swing`, `transport_mode`; swing parses float, others integer. |
| Module | `M,<slot>,<name>,<version>` | Loads module into slot; version is emitted as `module.version` (default 1) but ignored on load. |
| Parameter | `P,<name>,<value>` | Applies to the most recently loaded module by parameter name/type and invokes edit then exit callback. Button and UI-only parameter values are omitted. |
| Extra module attribute | `A,<name>,<value>` | Only declared `save_attrs` are written/restored; list/tuple/bytearray values use `|` delimiter. |
| Terminator | `END` | Stops record processing; blank lines also stop loading. |

Preserve `Scene01`..`Scene10`, 1-based `_activescene`, and the `SCN1` records for drop-in user-scene compatibility unless product requirements explicitly choose a versioned migration. Keep the legacy slot remap (slot 0 non-`set` becomes input slot 1; slot 1 `transport` becomes SET) when importing older scene files. Unknown parameter names and unknown `A` attributes are silently skipped, so additive module parameters remain backward-tolerant. Current loader does not verify saved module version, clamp restored parameter values itself, or recover malformed numeric records; these are observed behaviors, not guarantees to extend without a deliberate compatibility decision. Source: `circuitpython-source/src/apps/Orion/core/scenes.py:8-31, 87-128, 131-189, 215-303`.
