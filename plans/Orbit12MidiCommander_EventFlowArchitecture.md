# Orbit12MidiCommander — MIDI Event Flow Architecture

## Purpose

This document defines the runtime event-flow architecture for the **Orbit12MidiCommander** app.

The goal is to create a modular MIDI processing system where:

- note and timing events enter the system
- modules process those events in sequence
- each module can modify, suppress, expand, or delay events
- the final events are sent to MIDI output

This architecture is intended to be:
- simple
- deterministic
- easy to debug
- compatible with CircuitPython and RP2040 constraints

---

## High-Level Flow

The core runtime flow is:

```text
Input / Timing Sources -> Event Normalization -> Module Chain -> MIDI Output
```

Typical examples:

```text
Physical keys -> note events -> module 1 -> module 2 -> module 3 -> MIDI out
Transport tick -> tick event  -> module 1 -> module 2 -> module 3 -> MIDI out
```

---

## Core Principle

The system should use an **event pipeline**.

Each module receives one or more events, processes them, and returns zero or more events for the next module.

The handoff format between modules is:

```text
List[Event] in -> List[Event] out
```

This is the core rule for module-to-module communication.

Modules should **not**:
- call other modules directly
- send MIDI directly
- modify global state arbitrarily

Instead:
- the engine controls chain order
- modules only process events
- the output layer sends final MIDI

---

## Event Pipeline Model

### General Processing Rule

For every incoming event:

1. gather input events
2. normalize them into internal Event objects
3. pass them through each module in chain order
4. collect final output events
5. send those to MIDI output

Conceptually:

```python
events = source_events

for module in modules:
    events = module.process(events, context)

midi_out.send(events)
```

---

## Event Sources

The MIDI commander app should treat multiple inputs as event sources.

### 1. Musical Input Sources
These create note-related events.

Examples:
- physical key presses
- future external MIDI input
- internal sequencer modules
- generated triggers

### 2. Timing Sources
These create time-based events.

Examples:
- internal clock
- external MIDI clock
- transport start/stop/reset
- tick events

### 3. System / App-Level Sources
These should usually be handled outside the MIDI chain unless explicitly needed.

Examples:
- encoder movement
- button press for UI
- screen navigation
- preset load/save
- app switching

---

## Recommended Event Types

The chain should operate on normalized internal event objects.

### MIDI / Musical Events
- `note_on`
- `note_off`
- `cc`
- `pitch_bend` (future)
- `aftertouch` (future)

### Timing / Transport Events
- `tick`
- `transport_start`
- `transport_stop`
- `transport_reset`

### Input Mapping Events
These may exist before the input mapper converts them into notes.
- `key_down`
- `key_up`

---

## Event Object

A single shared Event type should be used throughout the system.

Recommended starter fields:

- `type`
- `note`
- `velocity`
- `value`
- `tick`
- `channel`
- `source`

### Example

```python
class Event:
    def __init__(
        self,
        type,
        note=None,
        velocity=None,
        value=None,
        tick=None,
        channel=0,
        source=None,
    ):
        self.type = type
        self.note = note
        self.velocity = velocity
        self.value = value
        self.tick = tick
        self.channel = channel
        self.source = source
```

### Notes
- not every event uses every field
- `type` is always required
- events should stay small and predictable
- prefer explicit fields over large arbitrary payloads

---

## Context Object

The chain also receives a shared context object.

The context stores shared persistent runtime state such as:

- current tick
- transport running
- bpm
- swing
- root note
- active scale
- MIDI output channel
- app-wide settings

### Important Rule

Modules should mostly **read** from context, not own or freely mutate it.

Context should be:
- owned by the engine/app
- passed into modules
- treated as shared world state

---

## Module Contract

Each module should follow the same API shape.

Recommended minimum interface:

```python
class ModuleBase:
    def reset(self):
        pass

    def process(self, events, context):
        return events
```

### Contract Rules

A module may:
- pass events through unchanged
- modify incoming events
- suppress events
- expand one event into many
- generate new immediate events
- schedule future events internally

A module should not:
- call later modules directly
- emit MIDI directly
- block/wait for time to pass

---

## How Modules Hand Off Data

### Recommended Handoff Format
The best handoff between modules is:

```text
List[Event]
```

Why:
- one input event may become many output events
- a module may remove events completely
- timing modules may emit nothing immediately
- chord modules may expand one note into several notes

### Example Flow

Input:

```text
[note_on C4]
```

After chord module:

```text
[note_on C4, note_on E4, note_on G4]
```

After transpose module:

```text
[note_on D4, note_on F#4, note_on A4]
```

This makes list-based handoff the most flexible and consistent approach.

---

## Chain Order

The engine owns chain order.

Example:

```text
InputMapper -> Scale -> Chord -> Arp -> Humanize -> Output
```

The engine is responsible for calling each module in sequence.

Modules must not know which module comes after them.

### Example Engine Logic

```python
current_events = incoming_events

for module in modules:
    current_events = module.process(current_events, context)

midi_out.send(current_events)
```

---

## Immediate vs Time-Based Modules

Modules fall into two broad groups.

### 1. Immediate Modules
These act on events right away.

Examples:
- transpose
- scale quantizer
- velocity mapper
- chord generator
- randomizer

### 2. Time-Based Modules
These depend on transport/ticks and may emit events later.

Examples:
- arpeggiator
- Euclidean sequencer
- ratchet
- note delay
- gate length processor

Time-based modules should not block.  
They should store state and/or schedule future events.

---

## Scheduling Future Events

Modules that need to emit events later should keep internal scheduled state.

### Example
An arp may:
- receive held notes now
- emit a note on tick 120
- emit note-off on tick 122

The module should store future actions like:

```text
[(122, note_off_event)]
```

Then on each tick, it checks what is due.

### Recommended Simple Scheduler Model
Inside a module:

```python
self.scheduled_events = []
```

Each item:

```python
(due_tick, event)
```

On tick:
- emit all due events
- keep future ones
- remove emitted ones

### Important Rule
Do not wait/block.  
Always schedule future events relative to ticks.

---

## Note Lifecycle and Ownership

This is one of the most important architectural concerns.

If a module creates note events, it must also ensure proper note-off handling.

Examples:
- chord module turns one note into three
- arp emits one note later and must turn it off later
- transport stop must not leave hanging notes

### Required Safety Behavior
The app/engine should support:
- panic / all notes off
- full reset on preset load
- full reset on app exit
- full reset on transport stop if appropriate

### Recommended Support
Add a central note registry later if needed for robust tracking.

---

## Input Mapping Layer

Raw hardware events should usually not go straight into the musical modules.

Recommended flow:

```text
physical keys -> key_down / key_up -> InputMapper -> note_on / note_off
```

This keeps hardware-specific behavior isolated.

The InputMapper can also implement:
- scale-locked layouts
- octave shifting
- shifted alternate key functions
- note generation from the 12 keys

---

## Separation of UI and MIDI Events

Not all app events should go through the MIDI chain.

### UI / System Events
Usually handled by app/UI layer:
- encoder_turn
- encoder_press
- back
- preset_load
- app navigation

### MIDI / Musical Events
Handled by module chain:
- note_on
- note_off
- tick
- transport events

This separation keeps the chain focused and easier to reason about.

---

## Main Runtime Loop

The MIDI commander app should run in a loop roughly like this:

```python
while True:
    input_events = input_manager.read_events()
    tick_events = transport.get_events()

    ui_events = []
    midi_events = []

    for e in input_events + tick_events:
        if e.type in ("encoder_turn", "encoder_press"):
            ui_events.append(e)
        else:
            midi_events.append(e)

    app.handle_ui_events(ui_events)

    output_events = chain_engine.process_events(midi_events, context)
    midi_out.send(output_events)

    app.render()
```

This is just a conceptual model.  
The exact implementation may vary, but the architecture should remain the same.

---

## Example Module Behaviors

### Transpose Module
- input: `note_on C4`
- output: `note_on D4`

### Chord Module
- input: `note_on C4`
- output: `note_on C4`, `note_on E4`, `note_on G4`

### Arp Module
- input: held notes
- immediate output: none
- on future ticks: emits one note at a time

### Humanizer Module
- input: note_on events
- output: slightly changed velocity or timing behavior

---

## Recommended MVP Scope

For the first working version of Orbit12MidiCommander, keep the event architecture simple:

- linear chain only
- no branching routes
- one shared Event type
- one shared Context object
- modules process `List[Event]`
- per-module internal scheduler where needed
- centralized MIDI output

### Good First Modules
- InputMapper
- Transpose
- Scale
- Chord
- Arp

---

## Architectural Rules Summary

1. Use internal Event objects as the module handoff format
2. Pass `List[Event]` between modules
3. The engine owns chain order
4. Modules do not call each other directly
5. Modules do not send MIDI directly
6. Context is shared state, passed into modules
7. Time-based behavior is scheduled, not blocked
8. Keep UI/system events separate from musical events
9. Use linear routing first
10. Always provide panic/reset behavior

---

## Final Summary

Orbit12MidiCommander should use a **linear, event-driven module chain**.

The core model is:

```text
normalized input events -> module 1 -> module 2 -> module 3 -> MIDI output
```

Each module receives a list of events, processes them, and returns a new list of events.

This approach provides:
- flexibility
- predictability
- easy debugging
- compatibility with both immediate and time-based MIDI behaviors

It is the recommended architecture for the first implementation of the MIDI commander app.
