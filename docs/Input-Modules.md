# Input Modules

Input modules live in the first slot of the chain. They turn the MacroPad keys into musical note streams before the rest of the chain processes them.

Global musical context now lives in the `Set` slot. Input modules inherit the remaining local performance controls from the base input class:

- `OCT`: octave
- `PADOFS`: pad offset, which shifts the keyboard around
- `VEL`: velocity
- `OP`: how this module's output behaves in the chain
- `OUTCH`: MIDI output channel
- `SRC`: source routing

## Note

`Note` is the simplest input module. Each pad maps directly into the current set key and scale plus the local input octave. It is the cleanest starting point if you want the MacroPad to behave like a scale layout instead of a specialized performance surface.

## Chords

`Chords` turns the 12 pads into chord roots and chord modifiers. It now supports two layouts:

- `SPLT`: 6 roots and 6 modifiers
- `4&8`: 4 modifiers on the left column and 8 roots on the remaining pads

The modifier editor follows the `Matrix` and `ChordWalk` pattern: `SEL` picks which modifier slot you are editing, `MOD` edits that slot's action, and pressing a modifier key updates the selector automatically when the input chain is the active UI section.

Visible module-specific parameters:

- `SEL`: which modifier slot you are editing
- `MOD`: action assigned to the selected modifier slot
- `BASS`: which bass note to add, if any
- `SPRD`: chord spread amount, moves upper notes up an octave
- `BRWSCL`: borrowed scale for borrowed chord modifier
- `MAXCT`: maximum number of simultaneous held chords
- `LAY`: chord pad layout
- `SPLDRP`: which scale degree to omit from the 6-root split layout

Parameter order:

- local input controls: `OCT`, `PADOFS`
- modifier editor: `SEL`, `MOD`
- chord controls: `BASS`, `SPRD`, `BRWSCL`, `MAXCT`
- shared tail control: `VEL`
- layout controls: `LAY`, `SPLDRP`

Enum parameters:

- `MOD`: `OFF`, `INV1`, `INV2`, `INV3`, `SEV`, `ADD9`, `SUS2`, `SUS4`, `PWR`, `DIM`, `BRRW`
- `BASS`: `NONE` no extra bass note, `ROOT` root an octave down, `2ND` second tone an octave down, `LOW` the lowest note an octave down, `HIGH` the highest note an octave down
- `SPRD`: `TIGHT` compact voicing, `MED` medium spread, `WIDE` wide spread
- `BRWSCL`: `AUTO` choose a borrowed scale automatically, otherwise pick one of the named scales listed below
- `LAY`: `SPLT` split roots/modifiers, `4&8` left-column modifiers with 8 root pads

## Drum

`Drum` turns the pad grid into a drum layout instead of a pitched layout. It supports several pad maps, including left/right shifted 12-note layouts and bottom-row variations.

Visible module-specific parameters:

- `LAY`: pad layout
- `PADOFS`: pad-to-note offset
- `VEL`: output velocity

This module also sets the UI key label to `DRUM` so the active input mode is obvious on the screen.

The available drum layouts are:

- `ORD`: the natural MacroPad order
- `L12`: a left-shifted 12-note layout
- `R12`: a right-shifted 12-note layout
- `B3R`: a bottom-three-rows layout in the normal orientation
- `B3F`: the same bottom-three-rows layout flipped

Enum parameters:

- `LAY`: `ORD` natural MacroPad order, `L12` left-shifted 12-note layout, `R12` right-shifted 12-note layout, `B3R` bottom-three-rows in the normal orientation, `B3F` bottom-three-rows flipped

## Matrix

`Matrix` is the most programmable input module. Each pad can behave as a note or a chord, and each pad can store its own note degree, chord toggle, sevenths, ninths, inversion, sus chord voicing, and power-chord state.

Visible module-specific parameters:

- `PAD`: which pad you are editing
- `NOTE`: scale degree for that pad
- `CHORD`: whether the pad plays a chord or a single note
- `7TH`: add a seventh
- `9TH`: add a ninth
- `INV`: pad-specific inversion count
- `SUS`: sus chord voicing
- `PWR`: power-chord mode
- `BASS`: global bass handling
- `SPRD`: global chord spread
- `RESET`: restore one of the preset layouts

`Matrix` is useful when you want the pads to behave like a custom song layout, a buffet of musical ingredients, instead of a fixed playing surface.

Enum parameters:

- `BASS`: `NONE` no extra bass note, `ROOT` root an octave down, `2ND` second tone an octave down, `LOW` the lowest note an octave down, `HIGH` the highest note an octave down
- `SPRD`: `TIGHT` compact voicing, `MED` medium spread, `WIDE` wide spread
- `SUS`: `OFF` no suspension, `SUS2` sus2 voicing, `SUS4` sus4 voicing
- `RESET`: `>` arm the reset prompt, `DEG` load the degree-based layout, `CHRD` load the all-chord layout, `NOTE` load the note-only layout, `HALF` load the half-and-half layout

## Note Walk

`NoteWalk` turns the held pads into a moving note line. The first pad establishes the root, and subsequent pads shift the walk by the pad distance from the first held pad.

It does not add extra module-specific parameters beyond the shared musical controls. The result is best described as a scale-aware walking lead line.

`NoteWalk` and `ChordWalk` are both inspired by [Note Walker](https://osterhousesounds.com/products/note-walker) VST.

## Chord Walk

`ChordWalk` is the chord version of `NoteWalk`. Four pads on the right set the note register to those of the set chord (assigned similar to the matrix module), and the remaining pads are used as trigger pads that move the active chord tone through the held shape.
Chord pads act as latched selectors, and don't need to be held.

Visible module-specific parameters:

- `PAD`: which chord slot you are editing
- `NOTE`: root degree for that chord pad
- `7TH`: include a seventh
- `SUS`: sus chord voicing
- `PWR`: power-chord mode
- `PRVCRD`: preview the chord shape when pressed

This module is useful if you want pad-triggered chord motion instead of a simple walk in scale.

Enum parameters:

- `SUS`: `OFF` no suspension, `SUS2` sus2 voicing, `SUS4` sus4 voicing

## Scale Reference

The `SCL` control selects from these scale families across the input modules and scale-aware behaviors:

- Chromatic
- Major / Ionian
- Natural Minor / Aeolian
- Harmonic Minor
- Melodic Minor
- Dorian
- Phrygian
- Lydian
- Mixolydian
- Locrian
- Major Pentatonic
- Minor Pentatonic
- Suspended Pentatonic
- Blues
- Minor with major 6
- Major with flat 7
- Whole Tone
- Diminished Half-Whole
- Diminished Whole-Half
