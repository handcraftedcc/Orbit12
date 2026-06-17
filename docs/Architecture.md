# Orbit12 Architecture

Orbit12 is the project umbrella. It currently ships with one app, Orion, and the code is split into a small set of runtime entry points, shared core services, and two module families. Future apps can reuse the same project layout without changing the overall board-level entry point.

## Runtime Entry Points

`src/code.py` is the board entry point. It shows the launch screen, prepares the display, imports Orion, and starts the app.

`src/apps/Orion/Orion.py` is the app manager. It wires hardware, state, transport, UI, note routing, scene loading, and the main event loop together.

`src/apps/Orion/core/orion_macropad.py` wraps the MacroPad hardware into a stable object with display, encoder, keys, MIDI, and NeoPixel access.

## Core Services

The `core/` package owns the reusable parts of the app:

- `state.py`: global app state, chain slots, scene state, and active UI state
- `module.py`: shared module base classes and routing helpers
- `module.py` also defines `ModuleHelper`, the shared reference bundle passed into modules
- `parms.py`: parameter model and parameter types
- `note_array.py`: fixed-size note containers and note relationships
- `input.py`: keyboard and encoder scanning
- `output.py`: MIDI output queueing and channel handling
- `transport.py`: clock, BPM, swing, and MIDI clock transport
- `music.py`: scales, rates, patterns, and note helpers
- `scenes.py`: scene save/load logic
- `ui.py`: display groups, screen refresh, and the visible UI
- `neo_pixels.py`: key lighting and indicator handling
- `utils.py`: shared math and random helpers

## Module Families

`src/apps/Orion/inputmodules/` contains the input modules. These are the modules that live in the input slot and turn the pad grid into note streams.

`src/apps/Orion/modules/` contains the processing modules. These sit in the middle of the chain and transform note streams.

`src/apps/Orion/core/_modules/` contains the fixed chain slots: set, input, output, settings, and empty.

The registries in `inputmodules/_registry.py` and `modules/_registry.py` are the lazy-load dispatch tables. They keep the memory footprint down by importing a module only when it is actually selected.

## Data Flow

The main loop reads input, updates transport, runs the note stream through the chain, sends MIDI out, and then services any queued UI work. UI work is intentionally throttled so it does not compete with the clock.

The note flow is:

`input slot` -> `user module 1` -> `user module 2` -> `user module 3` -> `user module 4` -> `user module 5` -> `user module 6` -> `output slot`

The set and settings slots are visible control slots around that stream.

## State and Persistence

Scenes capture both app-level state and module state.

- Global values such as key, scale, octave, BPM, swing, and transport mode are stored in scene files. The `Set` slot is the UI owner for key, scale, BPM, swing, and transport mode.
- Module values are saved through the parameter system and optional `save_attrs`.
- Loading a scene rebuilds the chain by loading the required modules on demand.
- Resetting the scene unloads the current modules, restores defaults, and rebuilds the stock chain.

Scene data lives under `/userdata/Orbit12Orion/scenes/`.

- Each scene is stored as `Scene01` through `Scene10`.
- The active scene pointer is stored in `/userdata/Orbit12Orion/scenes/_activescene`.
- Scene files use a plain text record format:
  - `SCN1` header
  - `S,<attr>,<value>` for global state
  - `M,<slot>,<module>,<version>` for loaded modules
  - `P,<parm>,<value>` for saved parameter values
  - `A,<attr>,<value>` for extra saved module attributes
  - `END` terminator

User settings live under `/userdata/Orbit12Orion/user_settings`.

- The file is plain text `name,value` lines.
- Right now the only persisted user setting is `key_brightness`.

Scene changes are applied by saving the current scene, setting a new active scene, writing the active-scene file, saving user settings, and then rebooting the device with `supervisor.reload()`. That reboot is deliberate: it clears RAM held by the current module chain before the new scene loads.

## Memory Model

This project is designed for an RP2040 running CircuitPython, which means memory is the main architectural constraint.

The codebase uses a few common strategies to stay inside the limit:

- modules are lazily imported and explicitly unloaded when possible
- note containers are fixed-size arrays instead of general Python lists
- UI rebuilds are queued instead of redrawn on every tiny change
- display refresh is deferred until the UI state is ready
- `gc.collect()` is used deliberately at allocation boundaries

## Build Output

`src/` is the working source tree. It is what contributors edit, and it is also what the build helpers compile into `src_mpy/`.

`src_mpy/` is the build output tree that should be copied to the MacroPad when you want the lower-memory `.mpy` deployment. Loading raw `.py` files directly onto the board works for development, but compiled `.mpy` files are the recommended runtime format because they reduce memory pressure.

The repository's `tools/` folder includes helpers for both compilation and sync, including a PyCharm settings zip that installs "CircuitPython" external tools - a set of macros for building and updating files on the MacroPad while you work.
