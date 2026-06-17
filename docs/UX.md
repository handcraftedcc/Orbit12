# Orbit12 UX

This guide describes the Orion app, which is currently the only app in Orbit12. Orion is built around a chain of slots on the MacroPad display. The user moves through the chain with the encoder, edits one slot at a time, and uses the 12 keys both as musical input and as shortcuts when the knob is held.

## Chain Model

The visible chain is:

`S` -> `I` -> `1` -> `2` -> `3` -> `4` -> `5` -> `6` -> `O` -> `*`

- `S` is the set slot.
- `I` is the input slot.
- `1` to `6` are the user module slots.
- `O` is the output slot.
- `*` is the settings slot.

Musical notes flow left to right through the input slot, then the six user slots, then the output slot. The set and settings slots are visible in the chain, but they are control surfaces rather than musical stages.

## Hardware and Timing Notes

Orbit12 runs on the RP2040, which means memory is tight and every major allocation matters. CircuitPython is a good fit for this workflow, but it is not a hard real-time runtime.

- MIDI timing is usable and musical, but not sample-accurate.
- Display work is throttled so it does not fight the clock.
- Garbage collection is part of normal operation.

That tradeoff is intentional. The app is designed to stay responsive enough for live playing without pretending to be a precision timing engine.

## UI Screens

Orbit12 has four main UI states:

- Chain view: shows the full chain and the active slot cursor.
- Parameter selection: shows the active module's parameter list.
- Parameter edit: shows the same parameter list, but the active row is in edit mode.
- Module selection: shows the picker for swapping the current slot's module.

## UI Elements

- Header: chain id, module label, key/scale info, and chain preview.
- Footer: scene number and state icons.
- Chain strip: shows the active slot and swap state.
- Parameter panel: shows up to four parameters at a time, with the active row enlarged.
- Module selector: shows the current module choice for the selected slot.

The screen refresh is deferred until the UI state is ready, so the display does not flash through partial states while the app is rebuilding its groups.

## Operation Modes

Each processing slot can choose how it behaves through `OP`:

| Mode | Meaning |
| --- | --- |
| `NEXT` | Process the incoming notes and pass the transformed result to the next slot. |
| `ADD` | Process the incoming notes, then add the original notes back into the stream. |
| `OUT+N` | Send the processed notes to the module's MIDI output channel and pass the processed stream onward. |
| `OUT+S` | Send the processed notes to the module's MIDI output channel, but pass the original stream onward unchanged. |
| `SKIP` | Leave the stream unchanged. |

The `OUTCH` parameter sets the MIDI output channel for notes emitted directly by that module. The UI shows channels as 1 to 16.

## Source Routing

The `SRC` parameter lets a module read note input from an earlier stage in the chain instead of from its immediate predecessor.

- `PREV` uses the previous module's output.
- `IN` reads from the input slot.
- `S1` to `S5` read from earlier user slots.

Source routing only points backward. A module never reads from a later slot.

## Scenes

Scenes are saved project states. A scene stores the global musical settings and the modules currently loaded into the chain, along with the module parameters that are marked for persistence.

- The project supports 10 scenes.
- Scene data is stored under `/userdata/Orbit12Orion/scenes`.
- Switching scenes reloads the app.
- Scene change, save, copy, and reset actions are handled from the settings slot.

## Keyboard Shortcuts

Holding the knob changes the keys from musical input to shortcuts.

### Knob Held

- Key 1: stop transport
- Key 2: start transport
- Key 4: open module selection for the active slot
- Key 5: return to chain view
- Key 7: toggle the navigation key mode
- Key 9: switch between navigation and edit/selection behavior

### Note Navigation Mode

When navigation mode is set to notes:

- Key 8: octave up
- Key 11: octave down
- Key 10: key offset down
- Key 12: key offset up

### Parameter Navigation Mode

When navigation mode is set to parameters:

- In chain view, keys 8 and 11 move up and down through the chain/parameter boundary, while 10 and 12 move left and right through the chain.
- In module selection, the four corner keys step through available modules.
- In parameter selection, 8 and 11 move through parameters, and 10 and 12 move to adjacent chain slots.
- In parameter edit, the four corner keys adjust the current parameter value.

### Chain Swap

Holding the knob in chain view for about one second arms chain swap mode. Releasing after that enters swap mode, which lets the encoder move modules between slots instead of just changing the active slot.
