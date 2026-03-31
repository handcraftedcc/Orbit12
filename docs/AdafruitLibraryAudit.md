# Adafruit Library Audit (Bundle: `adafruit-circuitpython-bundle-py-20260328`)

## Scope

Checked requested areas:

- event handling
- MIDI
- timing/tick utilities
- display helpers for compact UI

Also checked `DeviceFiles/lib` for what is already present on the device mirror.

## Already Available (Reuse These)

## Input/Event Handling

- `adafruit_macropad`:
  - key event queue via `macropad.keys.events.get()`
  - encoder position via `macropad.encoder`
  - encoder switch + debounced switch helper
- `adafruit_debouncer`:
  - robust debouncing (`Debouncer`, `Button`)

## Timing

- `adafruit_ticks`:
  - `ticks_ms()`
  - wrap-safe `ticks_diff()`, `ticks_add()`, `ticks_less()`

## MIDI

- `adafruit_midi` core send/receive support
- message classes already included:
  - `note_on`, `note_off`, `control_change`, `program_change`, `pitch_bend`
  - transport/clock: `start`, `stop`, `midi_continue`, `timing_clock`
  - additional pressure/sysex classes
- note string parsing support exists in MIDI message parsing (e.g. `"C4"`)

## Display/Text

- `adafruit_display_text`
- `adafruit_simple_text_display`
- `adafruit_macropad.display_text(...)` convenience helper

## Already Present in `DeviceFiles/lib`

The device mirror already includes `.mpy` versions of:

- `adafruit_macropad`
- `adafruit_debouncer`
- `adafruit_ticks`
- `adafruit_midi/*`
- `adafruit_display_text/*`
- `adafruit_simple_text_display`
- dependencies (`neopixel`, `adafruit_pixelbuf`, `adafruit_bitmap_font`, `adafruit_hid`)

## Not Provided by Adafruit (Orbit12 Should Implement)

1. Orbit12-specific parameter schema and parser (`ui.json`)
2. Parameter render/edit engine with type-specific behavior
3. String editor UX (`✓`, `←`, `✗`, `A-Z`, `0-9`, wrapping)
4. Conditional visibility expression evaluator (`viscondition`)
5. Launcher app discovery/execution policy
6. ParamLab app configuration and validation set

## Reimplementation Guidance

Do not recreate generic MIDI transport, key debouncing, or wrap-safe tick math from scratch.
Use Adafruit libraries first, then build Orbit12-specific logic on top.
