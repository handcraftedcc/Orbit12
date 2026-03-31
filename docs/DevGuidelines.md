# Orbit12 Development Guidelines (CircuitPython + MacroPad RP2040)

## Target Platform

- Board: Adafruit MacroPad RP2040
- MCU: RP2040
- RAM: ~264 KB SRAM
- Flash on board: 8 MB total (shared by firmware, libraries, app code, and user data)

## Core Constraints

1. Flash is limited.
2. RAM is limited and fragmented easily.
3. CPU is capable but should not be wasted in busy redraw loops.

## Code Style for Constrained Hardware

1. Keep files/modules compact and focused.
2. Prefer simple data structures over deep nested objects.
3. Avoid per-frame allocations in the main loop.
4. Reuse buffers/lists where practical.
5. Avoid recursion and heavy metaprogramming.
6. Avoid dynamic imports in loop-time code paths.

## Runtime Architecture Rules

1. Single-threaded loop; no threads/async required.
2. Event-driven input handling.
3. Explicit update cadence for UI and timing tasks.
4. Redraw only when data or selection changed.

## Storage Rules

1. All mutable data belongs in `/userdata`.
2. Batch writes and avoid writing every encoder tick.
3. Write settings on explicit save, app exit, or debounced change windows.
4. Keep JSON compact and stable in schema.

## Library Reuse Policy

Do not reimplement functionality already provided by stable Adafruit libraries unless there is a concrete gap.

Already available and preferred:

- `adafruit_macropad` (keys/events, encoder, display helpers, MIDI helpers)
- `adafruit_debouncer` (button debouncing)
- `adafruit_ticks` (wrap-safe millisecond timing)
- `adafruit_midi` (MIDI send/receive + message classes)
- `adafruit_display_text` and `adafruit_simple_text_display` (text UI primitives)

## Orbit12 Library Placement

- Shared project libraries go in `/lib`, specifically:
  - `lib/orbit12ui/` for the UI parameter system
- App code goes in `/apps/<AppName>/`
- User configuration and presets go in `/userdata/<AppName>/`

## DeviceFiles as Source Mirror

- `DeviceFiles/` is the canonical mirror of deployed filesystem layout.
- Source files remain `.py` during development.
- Export step can compile `.py` to `.mpy` for deployment optimization.
