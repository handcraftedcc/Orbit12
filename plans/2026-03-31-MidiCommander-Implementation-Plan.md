# MidiCommander MVP Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the first functional `MidiCommander` app as a modular MIDI chain with input mapping, expandable module slots, transport/modifier controls, output routing, and key-lighting plumbing.

**Architecture:** Build a test-first, event-driven engine in `DeviceFiles/apps/MidiCommander` that separates input/control handling from MIDI-domain event processing. Reuse `orbit12ui` for menu navigation and schema handling, with module-local UI schemas loaded dynamically. Keep hardware-specific logic thin and deterministic for CircuitPython constraints.

**Tech Stack:** CircuitPython (MacroPad RP2040), `adafruit_macropad`, `adafruit_midi`, `adafruit_ticks`, host-side `unittest`, existing `orbit12ui`.

---

### Task 1: Add failing tests for core event and settings models

**Files:**
- Create: `tests/test_midicommander_event_model.py`
- Create: `tests/test_midicommander_settings.py`
- Create: `DeviceFiles/apps/MidiCommander/core/event_model.py`
- Create: `DeviceFiles/apps/MidiCommander/core/settings_model.py`

**Step 1: Write the failing tests**

Add tests for:
- `Event` structure defaults and field coercion
- transport event types (`transport_start`, `transport_stop`, `tick`)
- settings defaults for global/input/output/slots
- scale/key/mapping option validation

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_event_model.py tests/test_midicommander_settings.py -v`  
Expected: FAIL because core models do not exist yet.

**Step 3: Write minimal implementation**

Implement typed lightweight models (dict-backed or small classes) with strict defaults and option guards.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_event_model.py tests/test_midicommander_settings.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_event_model.py tests/test_midicommander_settings.py DeviceFiles/apps/MidiCommander/core/event_model.py DeviceFiles/apps/MidiCommander/core/settings_model.py
git commit -m "test+feat: add midicommander event and settings models"
```

### Task 2: Implement key-grid mapping and input note mapping logic

**Files:**
- Create: `tests/test_midicommander_keymap.py`
- Create: `tests/test_midicommander_input_mapper.py`
- Create: `DeviceFiles/apps/MidiCommander/core/key_grid.py`
- Create: `DeviceFiles/apps/MidiCommander/core/input_mapper.py`

**Step 1: Write the failing tests**

Test:
- logical-to-physical map equals `[9,10,11,6,7,8,3,4,5,0,1,2]`
- semitone ordering bottom-left to top-right
- `Note Chrom` mapping behavior
- `Note InScale` mapping behavior for major and minor scales
- `Drum` mapping forces base at `C1`, independent of key/scale
- `Shift` and `Octave` application in allowed modes only

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_keymap.py tests/test_midicommander_input_mapper.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement key-grid conversion and deterministic mapper functions with no hardware dependency.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_keymap.py tests/test_midicommander_input_mapper.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_keymap.py tests/test_midicommander_input_mapper.py DeviceFiles/apps/MidiCommander/core/key_grid.py DeviceFiles/apps/MidiCommander/core/input_mapper.py
git commit -m "test+feat: add key grid ordering and input note mapper"
```

### Task 3: Implement internal clock/transport with swing support

**Files:**
- Create: `tests/test_midicommander_clock.py`
- Create: `DeviceFiles/apps/MidiCommander/core/clock.py`
- Create: `DeviceFiles/apps/MidiCommander/core/transport.py`

**Step 1: Write the failing test**

Test:
- start/stop state transitions
- tick cadence from selected rate (`1/16` default)
- swing adjustment alternates off-beat tick timing when `swing > 0`
- swing has no effect when transport stopped

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_clock.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement non-blocking, tick-driven internal clock using `adafruit_ticks`-compatible abstraction and transport controls.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_clock.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_clock.py DeviceFiles/apps/MidiCommander/core/clock.py DeviceFiles/apps/MidiCommander/core/transport.py
git commit -m "test+feat: add internal clock and transport state machine"
```

### Task 4: Implement module host contract and slot engine

**Files:**
- Create: `tests/test_midicommander_module_host.py`
- Create: `tests/test_midicommander_chain_engine.py`
- Create: `DeviceFiles/apps/MidiCommander/core/module_host.py`
- Create: `DeviceFiles/apps/MidiCommander/core/chain_engine.py`
- Create: `DeviceFiles/apps/MidiCommander/modules/base/module.py`

**Step 1: Write the failing tests**

Test:
- module discovery from `modules/<id>/module.py`
- loading module UI from `module-ui.json` or `ui.py`
- empty slots pass events unchanged
- ordered slot processing (`List[Event] -> List[Event]`)
- module failure isolation (engine returns safe pass-through and error flag)

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_module_host.py tests/test_midicommander_chain_engine.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement module loading, slot container, and linear chain execution with deterministic order.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_module_host.py tests/test_midicommander_chain_engine.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_module_host.py tests/test_midicommander_chain_engine.py DeviceFiles/apps/MidiCommander/core/module_host.py DeviceFiles/apps/MidiCommander/core/chain_engine.py DeviceFiles/apps/MidiCommander/modules/base/module.py
git commit -m "test+feat: add module host and chain slot engine"
```

### Task 5: Implement modifier-control command layer

**Files:**
- Create: `tests/test_midicommander_modifier_actions.py`
- Create: `DeviceFiles/apps/MidiCommander/core/modifier_actions.py`
- Modify: `DeviceFiles/apps/MidiCommander/core/key_grid.py`
- Modify: `DeviceFiles/apps/MidiCommander/core/transport.py`

**Step 1: Write the failing test**

Test:
- modifier inactive: key events remain note events
- modifier active + logical key 0 => `transport_stop`
- modifier active + logical key 1 => `transport_start`
- unknown modifier key returns no-op action

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_modifier_actions.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement data-driven modifier map and transport action dispatch.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_modifier_actions.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_modifier_actions.py DeviceFiles/apps/MidiCommander/core/modifier_actions.py DeviceFiles/apps/MidiCommander/core/key_grid.py DeviceFiles/apps/MidiCommander/core/transport.py
git commit -m "test+feat: add modifier hold key command mapping"
```

### Task 6: Implement key-lighting pipeline with module override plumbing

**Files:**
- Create: `tests/test_midicommander_lighting.py`
- Create: `DeviceFiles/apps/MidiCommander/core/lighting.py`
- Modify: `DeviceFiles/apps/MidiCommander/core/module_host.py`

**Step 1: Write the failing test**

Test:
- base lighting colors root note differently from non-root
- pressed key color overrides base
- module-provided hint layer overrides base for selected keys
- layer merge order is deterministic and per-key

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_lighting.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement `LightingEngine` that merges `base -> pressed -> module_hints` and returns 12 RGB tuples.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_lighting.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_lighting.py DeviceFiles/apps/MidiCommander/core/lighting.py DeviceFiles/apps/MidiCommander/core/module_host.py
git commit -m "test+feat: add lighting layers and module hint plumbing"
```

### Task 7: Build app UI schema and slot-management interactions

**Files:**
- Create: `tests/test_midicommander_ui_schema.py`
- Create: `tests/test_midicommander_slot_management.py`
- Create: `DeviceFiles/apps/MidiCommander/ui.py`
- Create: `DeviceFiles/apps/MidiCommander/core/slot_manager.py`

**Step 1: Write the failing tests**

Test:
- UI exposes folders `Global`, `Input`, `Slots`, `Output`
- global/input/output defaults match design
- slot list renders empty slots + `+ Slot`
- selecting empty slot opens picker list
- adding module appends new slot correctly

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_ui_schema.py tests/test_midicommander_slot_management.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement dynamic UI schema generator and slot manager state transitions.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_ui_schema.py tests/test_midicommander_slot_management.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_ui_schema.py tests/test_midicommander_slot_management.py DeviceFiles/apps/MidiCommander/ui.py DeviceFiles/apps/MidiCommander/core/slot_manager.py
git commit -m "test+feat: add app ui schema and slot management logic"
```

### Task 8: Add starter modules and output routing

**Files:**
- Create: `tests/test_midicommander_modules_starter.py`
- Create: `tests/test_midicommander_output_router.py`
- Create: `DeviceFiles/apps/MidiCommander/modules/thru/module.py`
- Create: `DeviceFiles/apps/MidiCommander/modules/transpose/module.py`
- Modify: `DeviceFiles/apps/MidiCommander/modules/chords/chords.py`
- Modify: `DeviceFiles/apps/MidiCommander/modules/arp/arp.py`
- Create: `DeviceFiles/apps/MidiCommander/core/output_router.py`

**Step 1: Write the failing tests**

Test:
- thru module returns input events unchanged
- transpose applies semitone offset
- chords expands note-on and tracks note-off mapping
- arp emits scheduled notes on ticks
- output router applies selected MIDI channel to outgoing note events

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_modules_starter.py tests/test_midicommander_output_router.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement starter module behaviors and channel-routing output adapter.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_modules_starter.py tests/test_midicommander_output_router.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_modules_starter.py tests/test_midicommander_output_router.py DeviceFiles/apps/MidiCommander/modules/thru/module.py DeviceFiles/apps/MidiCommander/modules/transpose/module.py DeviceFiles/apps/MidiCommander/modules/chords/chords.py DeviceFiles/apps/MidiCommander/modules/arp/arp.py DeviceFiles/apps/MidiCommander/core/output_router.py
git commit -m "test+feat: add starter modules and output channel routing"
```

### Task 9: Integrate app runtime loop in `code.py`

**Files:**
- Create: `tests/test_midicommander_app_loop.py`
- Modify: `DeviceFiles/apps/MidiCommander/code.py`
- Create: `DeviceFiles/apps/MidiCommander/app_runtime.py`

**Step 1: Write the failing test**

Test:
- loop separates UI events from MIDI-domain events
- key-down path produces mapped note events
- modifier path dispatches transport actions
- transport ticks run module chain and output routing
- lighting engine output is applied without exceptions

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_midicommander_app_loop.py -v`  
Expected: FAIL.

**Step 3: Write minimal implementation**

Implement integration loop, dependency wiring, and safe error handling to keep app alive.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_midicommander_app_loop.py -v`  
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/test_midicommander_app_loop.py DeviceFiles/apps/MidiCommander/code.py DeviceFiles/apps/MidiCommander/app_runtime.py
git commit -m "test+feat: integrate midicommander runtime loop"
```

### Task 10: Final verification and docs update

**Files:**
- Modify: `docs/CurrentStatus.md`
- Modify: `docs/Architecture.md` (only if app-path or ownership details changed)

**Step 1: Run focused app tests**

Run: `python3 -m unittest discover -s tests -p 'test_midicommander*.py' -v`  
Expected: PASS for all new MidiCommander tests.

**Step 2: Run full project test suite**

Run: `python3 -m unittest discover -s tests -p 'test_*.py' -v`  
Expected: PASS.

**Step 3: Run syntax verification**

Run: `python3 -m py_compile $(find DeviceFiles -name '*.py')`  
Expected: no syntax errors.

**Step 4: Update docs**

Document implemented MidiCommander capabilities and any known MVP limitations.

**Step 5: Commit**

```bash
git add docs/CurrentStatus.md docs/Architecture.md
git commit -m "docs: update status and architecture for midicommander mvp"
```

