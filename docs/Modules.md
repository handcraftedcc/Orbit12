# Modules

Orbit12 has two kinds of chain modules. Some slots are fixed system slots, and the rest are selectable processing modules.

## Fixed System Slots

### Empty

`Empty` is a placeholder slot. It does not transform notes. Its main purpose is to give you a place to pick another module.

Visible control:

- `EMPTY`: open the module picker

### Transport

`Transport` controls the clock. It switches between internal and external transport, sets BPM, and adds swing.

Visible parameters:

- `MDE`: internal or external clock mode
- `BPM`: transport tempo
- `SWNG`: swing amount

This slot is UI-only from a musical-routing point of view. It is where you manage the timing engine, not where you shape note data.

Enum parameters:

- `MDE`: `INT` internal clock, `EXT` external MIDI clock

### Output

`Output` is the final MIDI handoff point. It forwards the note stream to MIDI and can optionally print the current notes for debugging.

Visible parameters:

- `PRNT`: print the current note arrays
- `OUTCH`: final MIDI output channel

### Settings

`Settings` is where you manage scenes and a few project-wide preferences.

Visible parameters:

- `SCENE`: choose the active scene
- `SAVSCN`: save the current scene
- `CPYSCN`: copy the current chain into another scene
- `RSTSCN`: reset the scene or restore its layout
- `RAM`: print current memory use
- `KEYBRI`: change key brightness

Enum parameters:

- `CPYSCN`: choose the scene to copy into
- `RSTSCN`: `>` arm the reset prompt, `YES` confirm reset

## Performance Modules

### Arp

`Arp` is the core arpeggiator. It sorts the held notes, steps through them at the selected rate, and can follow several playback paths such as mirror, thumb, pinky, diverge, converge, random, or repeat.

Visible parameters:

- `RATE`: step rate
- `SORT`: note ordering
- `PLAY`: playback pattern
- `TRNS`: transposition in scale steps
- `MAXTRN`: maximum allowed transposition
- `BND`: how notes are bounded when transposed
- `GATE`: note length
- `PTN`: rhythm pattern
- `PTSHFT`: rhythm shift
- `RNDLEN`: randomization window
- `SEED`: random seed
- `RTRG`: retrigger mode

`PTN` and `PTSHFT` use the shared [Pattern Reference](#pattern-reference) below.

Enum parameters:

- `SORT`: `UP` low-to-high, `DOWN` high-to-low, `ORD` held order
- `PLAY`: `SORT` sorted order, `MIRR` mirror, `THUMB` thumb bounce, `PINKY` pinky bounce, `DIV` diverge, `CONV` converge, `CDIV` converge then diverge, `RND` random note per step, `RPT` repeat all held notes
- `BND`: `WRPU` wrap within the positive range, `WRPB` wrap across the signed range, `FLDU` fold within the positive range, `FLDB` fold across the signed range, `CLMP` clamp at the limit
- `RTRG`: `STBL` follow the transport grid, `CONT` continue the current sequence position, `RTRG` restart when a new held-note set begins

### ArpWalk

`ArpWalk` is an arpeggiator with a programmable walk pattern. Instead of one fixed playback path, it steps through a small pattern of offsets that can be edited or randomized.

Visible parameters:

- `RATE`
- `SORT`
- `LEN`: step pattern length
- `SEL`: selected step in the pattern
- `VAL`: value for the selected step
- `RAND`: randomize the step pattern
- `INV`: invert the walk direction
- `MAXWLK`: maximum walk distance
- `BND`
- `GATE`
- `RYTM`: rhythm pattern
- `PTSHFT`
- `RTRG`

`LEN`, `SEL`, `VAL`, and `RAND` edit the module's local walk pattern. `RYTM` and `PTSHFT` use the shared [Pattern Reference](#pattern-reference) below.

Enum parameters:

- `SORT`: `UP` low-to-high, `DOWN` high-to-low, `ORD` held order
- `BND`: `WRPU` wrap within the positive range, `WRPB` wrap across the signed range, `FLDU` fold within the positive range, `FLDB` fold across the signed range, `CLMP` clamp at the limit
- `RTRG`: `STBL` follow the transport grid, `CONT` continue the current sequence position, `RTRG` restart when a new held-note set begins

### Bounce

`Bounce` repeats a note several times with decaying spacing, velocity, and gate length. It can run off fixed milliseconds or a musical rate.

Visible parameters:

- `BOUNCE`: number of repeats
- `MODE`: millisecond or rate-based spacing
- `INTMS`: base interval in milliseconds
- `INTRT`: base interval as a musical rate
- `GATE`: note length
- `INTMOD`: how quickly the interval decays
- `VELMOD`: how quickly velocity decays
- `GTEMOD`: how quickly gate length decays
- `MODCRV`: decay curve

Enum parameters:

- `MODE`: `MS` timing in milliseconds, `RATE` timing in musical rate steps
- `MODCRV`: `LIN` linear decay, `CUBIC` cubic decay, `RAND` randomized decay curve

### Chord

`Chord` adds up to five scale-aware interval offsets to each incoming note. It is the simplest chord builder in the processing chain.

Visible parameters:

- `OFS1` to `OFS5`: interval offsets
- `INSCL`: whether offsets follow the current scale

### Chopper

`Chopper` acts like a rhythmic gate. It decides, step by step, whether the currently held notes should sound or be silenced.

Visible parameters:

- `RATE`: gate rate
- `CHOP`: how many steps of the pattern are blocked
- `PTLEN`: pattern length
- `SEED`: deterministic random seed
- `RTRG`: retrigger mode

Enum parameters:

- `RTRG`: `STBL` follow the transport grid, `CONT` continue the current sequence position, `RTRG` restart when a new held-note set begins

`PTLEN` sets the local cycle length.

### Delay

`Delay` postpones note-ons and note-offs by a fixed amount. It can also add a small amount of deterministic random variation.

Visible parameters:

- `DLY`: base delay
- `DLRND`: random delay range

### Drop

`Drop` randomly removes some note-ons from the stream. It is useful when you want a lighter or more broken-up line without rewriting the source material.

Visible parameters:

- `DROP`: drop chance
- `MDE`: per-note or per-step randomization
- `PTLEN`: pattern length
- `SEED`: random seed

Enum parameters:

- `VELMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero
- `NTMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero
- `OCMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero
- `MDE`: `NOTE` drop per note, `STEP` drop once per step

`PTLEN` sets the local repeat window for step-based variation.

### Euclid

`Euclid` runs up to four lanes of Euclidean sequencing. Each lane has its own pulse count, length, and rotation, and each lane can be edited separately.

Visible parameters:

- `LANE`: which lane you are editing
- `RATE`: step rate
- `PULSES`: pulses for that lane
- `LENGTH`: length for that lane
- `ROT`: rotation for that lane
- `SORT`: note ordering
- `GATE`: note length
- `RTRG`: retrigger mode

Enum parameters:

- `SORT`: `UP` low-to-high, `DOWN` high-to-low, `ORD` held order
- `RTRG`: `STBL` follow the transport grid, `CONT` continue the current sequence position, `RTRG` restart when a new held-note set begins

### Gate

`Gate` passes notes through immediately and schedules their note-offs later. It is a simple way to control note length without changing pitch or order.

Visible parameters:

- `GATE`: base gate length
- `GTRND`: random gate variation
- `PTLEN`: pattern length

`PTLEN` sets the local cycle length for gate variation.

### Latch

`Latch` keeps notes held until they are explicitly toggled off or replaced by newer notes once the polyphony limit is reached.

Visible parameters:

- `POLY`: maximum held notes

### Pick

`Pick` selects a subset of the currently held notes and passes only that subset onward. You can pick from the start, end, low notes, or high notes, and you can invert the selection.

Visible parameters:

- `MDE`: selection order
- `COUNT`: number of notes to keep
- `INV`: invert the selection
- `OFST`: offset into the ordered note list
- `OCT`: octave shift for the picked notes

Enum parameters:

- `MDE`: `STRT` from the first held note, `END` from the last held note, `LOW` from low to high, `HIGH` from high to low

### Quantize

`Quantize` waits for the next grid step before releasing note-ons and note-offs. It keeps note lengths aligned to the transport grid.

Visible parameter:

- `RATE`: quantization grid

### Randomize

`Randomize` adds controlled variation to note, octave, and velocity. It can stay scale-aware, and it uses deterministic seeds so the results can repeat when the transport pattern repeats.

Visible parameters:

- `VELRNG`: velocity variation range
- `VELMDE`: velocity variation mode
- `NTRNG`: note variation range
- `NTINC`: note increment size
- `NTCHNC`: note variation chance
- `NTMDE`: note variation mode
- `INSCL`: scale-aware note variation
- `OCRNG`: octave variation range
- `OCCHNC`: octave variation chance
- `OCMDE`: octave variation mode
- `PTLEN`: pattern length
- `SEED`: random seed

Enum parameters:

- `VELMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero
- `NTMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero
- `OCMDE`: `UNI` one-way variation from zero to the maximum, `BI` two-way variation around zero

`PTLEN` sets the repeat window used for deterministic randomization.

### Retrigger

`Retrigger` turns held-note changes into repeated note-offs and note-ons when enough time has passed. It can respond to presses, releases, or both.

Visible parameters:

- `MODE`: trigger source
- `THRSH`: minimum retrigger interval

Enum parameters:

- `MODE`: `PRESS` retrigger on note-on changes, `REL` retrigger on note-off changes, `BOTH` retrigger on either

### Strum

`Strum` turns a held chord into a staggered roll. It can change the note order, tilt the velocity across the stack, and use different articulation shapes.

Visible parameters:

- `AMT`: strum spacing
- `ORDR`: note order
- `TILT`: velocity tilt across the strum
- `CNT`: how many notes receive tilt
- `ARTC`: articulation shape

Enum parameters:

- `ORDR`: `ORD` held order, `UP` low-to-high, `DOWN` high-to-low, `UPDWN` alternate direction each strum, `RAND` random note order
- `ARTC`: `EVEN` even spacing, `RNDLTE` random light spacing, `RNDSTR` random strong spacing, `ACCL` accelerate, `DECL` decelerate, `HUMN` humanized roll, `RAKE` rake shape, `SWNG` swing shape, `FLAM` flam shape

### Transpose

`Transpose` moves every note up or down by a fixed amount. It can work in raw semitones or stay aware of the current scale.

Visible parameters:

- `SEMI`: semitone shift
- `OCT`: octave shift
- `INSCL`: scale-aware mode

### Wander

`Wander` generates a wandering melodic line from the held notes. It is a controlled random walk with gravity, step limits, deviation limits, loop length, and optional rhythmic gating.

Visible parameters:

- `RATE`: step rate
- `GRAV`: how strongly the line returns to the center
- `MIN`: minimum step size
- `MAX`: maximum step size
- `DEV`: maximum allowed deviation
- `TENS`: extra directional tension
- `SEED`: random seed
- `LOOP`: loop length
- `RTRG`: retrigger mode
- `RTHM`: rhythm pattern
- `GATE`: note length

`RTHM` uses the shared [Pattern Reference](#pattern-reference) below.

Enum parameters:

- `RTRG`: `STBL` follow the transport grid, `CONT` continue the current sequence position, `RTRG` restart when a new held-note set begins

## Enum Reference

Shared module controls:

- `OP`
  - `NEXT`: pass the processed result to the next chain slot
  - `ADD`: combine the module output with the incoming notes
  - `OUT+N`: send the module output to MIDI and keep chaining
  - `OUT+S`: send the module output to MIDI and stop the chain there
  - `SKIP`: bypass the module and pass the input through unchanged
- `SRC`
  - `PREV`: use the previous chain slot as input
  - `IN`: use the raw input slot as input
  - `S1` to `S5`: tap an earlier chain slot directly

## Pattern Reference

The shared pattern tables from `music.py` are shown below using `X` for an active step and `O` for an empty step. These are used by `PTN`, `PTSHFT`, `RYTM`, and `RTHM`.

```text
Pattern1: XX
Pattern2: XO
Pattern3: XXO
Pattern4: XOO
Pattern5: XXXO
Pattern6: XXOO
Pattern7: XOOO
Pattern8: XXXXO
Pattern9: XXXOO
Pattern10: XXOOO
Pattern11: XOXOX
Pattern12: XOXOO
Pattern13: XXXXXO
Pattern14: XXXXOO
Pattern15: XXXOOO
Pattern16: XXOXOX
Pattern17: XOXOOX
Pattern18: XXXXXXXO
Pattern19: XXXXXXOO
Pattern20: XXXXXOOO
Pattern21: XXXXOOOO
Pattern22: XXXOOOOO
Pattern23: XOXXXXOX
Pattern24: XOOXOXOO
Pattern25: XOXXOOXO
```

## Note on the Registry

The module registry in this checkout also references `phraser`, but there is no matching source file in the tree. If that module is intended to ship, the source needs to be restored before the docs can cover it accurately.
