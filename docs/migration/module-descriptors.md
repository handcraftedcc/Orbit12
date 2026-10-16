# Module selector footer descriptions

Shown in the footer (bottom-left, 17 cells of the 6px font) while a module is highlighted in the module selection view. Code: `firmware/Orbit12/src/app/module_descriptors.hpp`; the table itself is generated from the `module.json` manifests into `firmware/Orbit12/src/modules/generated/descriptors_generated.hpp` (edit the manifest's `descriptor`, then run `tools/modules/gen_registry.py`). Test: `tests/native/t15_module_descriptors.cpp`.

Font rule: only characters in the renderer charset (`kFontChars` in `firmware/Orbit12/src/app/renderer.cpp`, the ui.py FONTCHARS order). Uppercase, digits, space and punctuation. Lowercase is not drawable.

Lookup: `module_descriptor(id_or_label)` matches the registry id first. A selector label is accepted only when it names exactly one entry. `CHRD` is shared by the `chord` processor and the `chords` input, so it returns nullptr and callers must pass the id.

| Label | Module id | Descriptor (<=17) | Source |
|---|---|---|---|
| NOTE | note | PADS IN SCALE | docs/Input-Modules.md (Note); written from code |
| CHRD | chords | CHORD ROOTS+MODS | docs/Input-Modules.md (Chords) |
| DRUM | drum | DRUM PAD LAYOUT | docs/Input-Modules.md (Drum) |
| MTRX | matrix | CUSTOM PAD MAP | docs/Input-Modules.md (Matrix: "custom song layout") |
| NOTEWLK | note_walk | WALKING NOTE LINE | docs/Input-Modules.md (Note Walk: "walking lead line") |
| CHRDWLK | chord_walk | CHORD TONE WALK | docs/Input-Modules.md (Chord Walk) |
| EMPTY | empty | NO PROCESSING | docs/Modules.md (Empty: "does not transform notes") |
| ARP | arp | ARPEGGIATES NOTES | docs/Modules.md (Arp) |
| ARPWLK | arp_walk | ARP WALK PATTERN | docs/Modules.md (ArpWalk) |
| BOUNCE | bounce | REPEATS, DECAYS | docs/Modules.md (Bounce) |
| CHRD | chord | STACKS INTERVALS | docs/Modules.md (Chord: interval offsets) |
| CHOPPR | chopper | CHOPS HELD NOTES | docs/Modules.md (Chopper) |
| DELAY | delay | DELAYS NOTES | docs/Modules.md (Delay) |
| DROP | drop | DROPS NOTE-ONS | docs/Modules.md (Drop) |
| EUCLID | euclid | EUCLIDEAN RHYTHM | docs/Modules.md (Euclid) |
| GATE | gate | SETS NOTE LENGTH | docs/Modules.md (Gate) |
| LATCH | latch | HOLDS NOTES | docs/Modules.md (Latch) |
| PICK | pick | PICKS SUBSET | docs/Modules.md (Pick) |
| QNT | quantize | SNAPS TO GRID | docs/Modules.md (Quantize) |
| RND | randomize | RANDOM VARIATION | docs/Modules.md (Randomize) |
| RTRGR | retrigger | RETRIGGERS NOTES | docs/Modules.md (Retrigger) |
| STRUM | strum | ROLLS CHORDS | docs/Modules.md (Strum) |
| TRNS | transpose | SHIFTS PITCH | docs/Modules.md (Transpose) |
| WANDR | wander | RANDOM WALK LINE | docs/Modules.md (Wander) |

Notes:
- The `phraser` entry is hidden in the firmware and has no descriptor.
- The selector labels come from the `kProcessorSelectors` and `kInputSelectors` tables, which are generated into `firmware/Orbit12/src/modules/generated/selectors_generated.hpp` from the manifests' `label` fields. The test checks them through `module_label_for`.
- Descriptions were taken from `circuitpython-source/docs/Modules.md` and `Input-Modules.md`, then checked against the C++ descriptors in `firmware/Orbit12/src/modules/core/`. No Python docstring gave a shorter one-line summary. Ids were taken from the `ModuleDescriptor` and `InputDescriptor` constants.


## Footer rule

In the module selection view the footer shows the highlighted module's description, looked up by registry id (`module_descriptor(id)`). The label is never used as the key: CHRD names both the chord processor and the chords input. Empty is listed as `NO PROCESSING`. Leaving the list restores the default footer (the active scene name).
