# MidiCommander Modular MIDI Chain - Product And Technical Design

## Purpose

Define the MVP design for a modular MIDI-chain app in Orbit12 where the runtime flow is:

Input Keys + Transport -> Module Slots (N, user-expandable) -> Output

This document is the app-level design reference. The low-level event pipeline concept from `plans/Orbit12MidiCommander_EventFlowArchitecture.md` remains valid and is adopted here.

## App Naming And Paths

- Existing app folder is `DeviceFiles/apps/MidiCommander/`; MVP should continue with this path.
- User data path should be `DeviceFiles/userdata/MidiCommander/` on-device as `/userdata/MidiCommander/`.
- If the product name later becomes `Orbit12MidiCommander`, treat that as a rename/migration task after MVP stability.

## MVP Scope

1. Internal clock only.
2. Key input mapping and transport controls.
3. Expandable linear module slots.
4. Output channel routing.
5. Key lighting baseline + module override plumbing.
6. Module packaging contract (`module.py` + static or dynamic UI schema).

## Out Of Scope (Now)

1. External MIDI clock ingestion.
2. Branching or parallel module routing.
3. Complex preset browser UI.
4. Full module catalog (ship with a small starter set).

## Runtime Architecture

## Event Flow

1. Gather physical events (keys, encoder, encoder switch) and internal clock ticks.
2. Split into:
   - UI/control events
   - MIDI-domain events (note/tick/transport)
3. Normalize MIDI-domain events into `Event` objects.
4. Process through active module slots in order.
5. Route final events to output stage (channel + MIDI send).
6. Update key lighting from lighting pipeline.

## Event Sources

1. Input section (always keybed-originated note intent).
2. Transport/clock tick source.
3. Generator modules (for example sequencer module emitting notes without key press).

## UI Layout Model

Single app screen is a parameter list using `orbit12ui`, structured as top-level folders:

1. `Global`
2. `Input`
3. `Slots`
4. `Output`

### Global Section

- `Clock`: enum (`Internal`) for MVP
- `Rate`: `rate` parameter, default `1/16`
- `Swing`: float `0.0..1.0` (only active when clock is internal)

### Input Section

- `Scale`: enum; default `Chromatic`; include:
  - Chromatic
  - Major
  - Natural Minor
  - Melodic Minor
  - Mixolydian
  - Dorian
  - Lydian
  - Phrygian
  - Locrian
  - Pentatonic Minor
  - Pentatonic Major
  - Blues
- `Key`: enum; default `C`
- `Mapping`: enum:
  - `Note Chrom`
  - `Note InScale`
  - `Drum`
- `Shift`: int semitone shift (used only for `Note Chrom` and `Note InScale`)
- `Octave`: enum/int in `-3..3`

### Slots Section

- Render fixed visible slots plus `+ Slot` action row.
- Empty slot select opens module picker list.
- Filled slot select opens module config UI.
- User can append additional slots by activating `+ Slot`.
- Chain order is list order (top to bottom).

### Output Section

- `MIDI Channel`: 1..16
- Optional placeholders for future output destinations.

## Module Packaging Contract

Each module lives in:

`DeviceFiles/apps/MidiCommander/modules/<module_id>/`

Required:

- `module.py` (runtime logic, must expose module class/factory)

Optional UI sources:

- `module-ui.json` (static schema), or
- `ui.py` (dynamic schema using `build_ui()` / `UI_SCHEMA` / `TABS`)

Module contract:

1. Input: `List[Event]` + context
2. Output: `List[Event]`
3. No direct MIDI send
4. No direct calls to other modules
5. Optional lighting hints via dedicated lighting API (no direct NeoPixel writes)

## Key Mapping Rules

Logical note order must iterate from bottom-left to top-right, moving right first then up row-by-row.

For MacroPad physical IDs (top-left=0 .. bottom-right=11), define a `logical_to_physical` map:

- `[9, 10, 11, 6, 7, 8, 3, 4, 5, 0, 1, 2]`

Meaning:

- Logical index `0` (C) -> physical bottom-left key (`9`)
- Logical index increments semitone-by-semitone to the right, then continues on the row above.

## Modifier Control

Default behavior:

- Normal mode: keys are notes, encoder controls menu/edit.
- Modifier mode: hold encoder switch, then key presses trigger command actions.

MVP action map:

- Bottom-left key (logical 0): transport stop
- Next key right (logical 1): transport start
- Remaining modifier keys reserved for future transport/system actions

Implementation should use a data-driven map so more combos can be added without changing core input loop logic.

## Key Lighting Model

Baseline lighting rules:

1. Root-note keys use a distinct root color.
2. Non-root playable keys use default playable color.
3. Pressed keys use pressed color while active.

Required plumbing for future module override:

1. Lighting engine composes layers:
   - base layout layer (root/scale)
   - pressed state layer
   - module hint layers (optional)
2. Highest-priority non-empty layer color wins per key.
3. Modules can publish hint colors (for example chord tones) without owning physical LED writes.

MVP includes the plumbing and tests but does not require advanced module lighting behaviors.

## Data Model And Persistence

Persist app state in `/userdata/MidiCommander/settings.json`:

- global settings
- input mapping settings
- output settings
- slot list (module IDs + per-module settings)

Potential future presets path:

- `/userdata/MidiCommander/presets/*.json`

## Starter Module Set For MVP

1. `thru` (identity)
2. `transpose`
3. `chords` (triad/basic chord expansion)
4. `arp` (simple clocked arpeggiator)

These are enough to validate pipeline behavior and module-slot UX before adding sequencer/ratchet/echo families.

## Acceptance Criteria

1. Pressing keys produces mapped note events with correct key order and scale/mapping behavior.
2. Internal clock emits deterministic ticks at selected rate and swing amount.
3. Slots support empty -> pick module -> configure -> process chain.
4. Output stage applies selected MIDI channel to outgoing note events.
5. Modifier hold + key commands trigger start/stop transport actions.
6. Root/pressed lighting works and module-lighting override API exists.
7. App remains responsive under single-loop RP2040 constraints.

