# Module Development Guide

This guide is for contributors who want to add new input modules or processing modules to Orbit12.

Orbit12 runs on a small CircuitPython target, so the first rule is simple: keep modules short, reuse memory, and avoid hot-path allocations.

For local development, use the CircuitPython `mpy-cross` compiler, not the MicroPython one. The wrong compiler will produce incompatible output for this runtime.

## Pick the Right Base Class

Use `Input` from `core/_modules/_input.py` for the input slot. Use `Module` from `core/module.py` for processing slots.

`Input` already gives you the shared musical controls and the input-slot routing behavior. `Module` gives you the general parameter model, out-channel handling, source routing, and the shared processing wrapper.

## Shared Context

Every module receives a `ModuleHelper` object through `module_helper`.

That object is the shared reference bundle for the runtime:

- `orion`: the app manager
- `macropad`: hardware wrapper
- `state`: global app state
- `input_manager`: keyboard and encoder input
- `output_manager`: MIDI output queueing
- `transport`: clock and timing state
- `ui_manager`: UI manager
- `neo_pixels`: key light manager

Use `module_helper` when a module needs to talk to the rest of the app. Do not invent your own global references.

## The Processing Contract

Every module processes note arrays, not individual key events.

`process(note_ons, note_offs)` receives the upstream `NoteOnArray` and `NoteOffArray` objects and returns the module's own `note_ons_out` / `note_offs_out` objects. The incoming arrays belong to the previous stage in the chain, so do not modify them in place or keep them as your module's outputs.

The arrays are small, fixed-size containers with explicit lengths, so you should treat them like bounded buffers rather than general-purpose Python lists. A module should create and reuse its own output arrays and write the transformed notes there before returning them.

The pattern used throughout the codebase is:

1. Clear your module's output arrays at the top of `process()`.
2. Read the incoming arrays.
3. Write the transformed result into the output arrays.
4. Return the output arrays.

Do not allocate new output arrays in every call if you can avoid it.

## Routing Is Already Handled

The base `Module.process_super()` method wraps the module-specific `process()` call with:

- source routing
- operation mode handling
- direct MIDI output on the module's output channel

That means your module-specific `process()` should focus on musical behavior, not on the shared routing modes. If you need the original input stream, transform it and return it. If you need to emit directly to MIDI, let the base class do the channel handling.

## Parameters

Parameters are built with `Parms.Parm`.

Important fields:

- `name`: stable identifier for save/load and lookup
- `label`: short UI label
- `parm_type`: one of the parameter type classes
- `default`: initial value
- `minmax`: numeric bounds
- `increment`: jog step size
- `options`: enum choices
- `bind_object` and `bind_attribute`: auto-write the value to an attribute
- callbacks: `edit_callback_function`, `enter_callback_function`, `exit_callback_function`

Common parameter types:

- `IntParmType`
- `FloatParmType`
- `PercentParmType`
- `BooleanParmType`
- `EnumParmType`
- `ButtonParmType`
- `RateParmType`
- `NoteParmType`
- `PatternParmType`
- `StringParmType`

If a parameter's meaning or valid range changes based on another parameter, update the visible parameter object and call `queue_parm_rebuild()` when needed. That helper is a UI update request, not a UI element. It tells the parameter panel to rebuild on the next UI pass. See `arp_walk.py`, `euclid.py`, `matrix.py`, and `chord_walk.py` for examples.

## Note Arrays

The note containers in `core/note_array.py` are a major part of the architecture.

- `NoteArray` stores notes, with optional velocities, times, and channels.
- `NoteOnArray` is a `NoteArray` with velocity storage.
- `NoteOffArray` is a note container for releases.
- `NoteRelationshipArray` maps input notes to output notes so transformed notes can be turned off correctly later.

Use these instead of raw Python lists when you need to store active notes, scheduled events, or input-to-output mappings.

## Cleanup Rules

Implement `stop()` and `remove()` for any module that holds state.

- `stop()` should clear active musical state and pending output.
- `remove()` should break references that keep the module alive and then call `super().remove()` when appropriate.

This is important on CircuitPython because the primary reason for cleanup is to preserve memory when modules are removed or replaced. Stale references are often the reason a module cannot be unloaded cleanly.

## Scene Persistence

If a module has values that should survive save/load, expose them in one of two ways:

- make them parameters, which the scene loader can restore directly
- add their attribute names to `save_attrs` so `scenes.py` can serialize them

Examples:

- `matrix.py` saves its per-pad layout arrays
- `euclid.py` saves its lane arrays
- `arp_walk.py` saves its step pattern

## Memory Constraints

The existing modules show a few consistent patterns worth copying:

- keep per-module buffers on the instance
- use `bytearray` or fixed-size note arrays for simple sequences
- avoid building large temporary Python objects inside `process()`
- keep parameter names short
- prefer callbacks over expensive recomputation in the hot path
- use `gc.collect()` after major unload or load steps, not in every small method

If a feature needs a lot of code, that is often a sign that the feature should be simplified before it is added.

## Development Tooling

For board sync and local iteration:

- `src/` is the editable source tree
- `src_mpy/` is the compiled output tree
- the `tools/` folder contains build and sync helpers
- the bundled PyCharm settings zip is the recommended way to work on both `.py` and `.mpy` updates, because it wires up "CircuitPython" external tools - a set of macros for building and sending files to the MacroPad

For library dependencies, use the Adafruit CircuitPython bundle from [circuitpython.org/libraries](https://circuitpython.org/libraries).

## What to Copy from Existing Modules

When you add a new module, compare it to a nearby existing one:

- `Input` modules: `note.py`, `drum.py`, `matrix.py`, `chords.py`, `note_walk.py`, `chord_walk.py`
- rhythm modules: `gate.py`, `delay.py`, `bounce.py`, `quantize.py`, `retrigger.py`
- note-shaping modules: `transpose.py`, `randomize.py`, `pick.py`, `strum.py`, `latch.py`
- sequencer modules: `arp.py`, `arp_walk.py`, `euclid.py`, `wander.py`, `chopper.py`

That is the fastest way to stay consistent with the codebase's memory model and UI conventions.
