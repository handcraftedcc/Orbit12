# Foundation scene schema inventory

This note records the CircuitPython facts needed to finalize the native registry-derived SCN1 bounds. It deliberately does not assign a maximum line length or DTO capacity before the native descriptors exist.

## Source format and registry

`circuitpython-source/src/apps/Orion/core/scenes.py` writes one `SCN1` header, seven global `S,name,value` records, then a module header `M,slot,id,version` followed by zero or more `P,name,value` parameter records and `A,name,value` saved-attribute records, and finally `END`. Comma is the record separator and `|` separates array elements. `write_module` serializes every non-button parameter other than the five names in `SCENE_UI_PARMS`; it serializes only attributes named by each module's `save_attrs` tuple that are present on the module.

The source registries define 19 processor labels, of which 18 have import implementations because `phraser` is a label without an implementation; this is also recorded in `behavior-contracts.md`. `empty` is included in the 18 executable processor choices. The input registry contains six choices. Runtime slots are SET, IN, six processors, OUT, and SETTINGS: ten slots total (`core/constants.py`). Scene module rows are therefore bounded by the native slot schema, not by the number of registry labels.

## Declared saved attributes

The source declares saved arrays in four modules:

| Module | Attributes | Source-declared element bounds |
|---|---|---:|
| `matrix` | `notes`, `chords`, `sevenths`, `ninths`, `inversions`, `sus`, `power`, `dim` | 12 each; 96 scalar entries total |
| `chord_walk` | `notes`, `sevenths`, `sus`, `power` | 4 each; 16 scalar entries total |
| `euclid` | `pulses`, `lengths`, `rotations` | 6 each; 18 scalar entries total |
| `arp_walk` | `step_values` | 8 |

Evidence: `inputmodules/matrix.py` (`PAD_COUNT`, arrays, and `save_attrs`), `inputmodules/chord_walk.py` (`PAD_COUNT`, arrays, and `save_attrs`), `modules/euclid.py` (`LANES`, arrays, and `save_attrs`), and `modules/arp_walk.py` (`MAX_STEPS`, `DEFAULT_STEP_VALUES`, and `save_attrs`). All other implemented input and processor modules have no `save_attrs` declaration. The arrays above establish the fixed native schema element counts; decoders must reject excess elements instead of extending storage.

## Fixed native bounds

`tools/arduino/extract_scene_schema.py` statically walks constructors and `save_attrs` declarations, without importing board-dependent modules. Its JSON inventory is the source evidence for these maxima; it also reports zero `StringParmType` constructors. Constructor multiplicity accounts for literal `range` loops (including the five `chord.offsetN` parameters), while a selected dynamic parameter name contributes one field even when it can take several saved-attribute names.

The inventory gives a 21-character maximum persisted field name (`random_pattern_length`), 11-character attribute name (`step_values`), 10-character module name (`chord_walk`), 12 local persistable parameters in the densest processor (`arp_walk`, excluding its action button), eight attributes in `matrix`, and 12 elements in the largest saved array. The extractor adds inherited persisted controls: three controls per processor (`source_mode`, `operation_mode`, `out_channel`), and for inputs the three musical controls (`octave`, `key_offset`, `velocity`) when enabled plus `operation_mode` and `out_channel` (the input override calls the base output builder). `matrix` has eight generated pad controls in `PAD_VALUE_PARMS`, giving it 17 total persisted parameters; `chords` has 13 and `arp_walk` has 15. `drum` disables inherited musical controls. SCN1 parameter and attribute scalars are validated natively as int32, finite binary32, or boolean. Their encoded limits are 11 characters for signed int32, 15 for a finite float rendered with nine significant digits, and one character for booleans. The largest record is an `A` line: `A,` + 11 + `,` + (12×11 + 11 separators) = 157 characters. CRLF adds two bytes, so a maximum line occupies 159 bytes.

The largest valid scene uses the slot schema, not every source module at once: one header, seven globals, ten module rows, five SET parameters, up to 17 IN parameters, six processors with up to 15 parameters each, two OUT parameters, and SETTINGS with none. For attributes, IN can contribute up to eight rows (`matrix`), while each of six processor slots can contribute up to three (`euclid`), for 26 rows. Including `END`, this is 159 records. The input edit banks add up to 76 records (6 `BANK` headers, 58 `BP` parameter slots of the six inputs including their global slots, 12 `BA` attributes; see `storage.md`, "Input banks"). Eight format-headroom rows are reserved, giving a bounded maximum of `(159 + 76 + 8) × 159 = 38,637` bytes. The `scene_schema.hpp` constants and static assertions encode these bounds. They conservatively combine source maxima across mutually exclusive module selections.

The Python writer alone cannot provide safe typed bounds: integer parameters without `minmax` are not globally bounded in Python, and its loader can replace saved lists with a different element count. The native importer validates every field against its schema, rejects excess array elements, and stages the complete scene before committing it.

## T01/T07 status

`types.hpp` contains the shared fixed capacities already stated by the architecture and contracts (`12` keys/polyphony, `6` processor slots, `24` output notes, `64` input records, and `10` scenes), plus compact note and buffer alignment assertions. The extractor output is the reproducible source inventory; it includes source modules that remain unavailable for native execution. The native registry must advertise only implemented modules, while the scene importer can retain the source schema inventory to validate or diagnose saved scenes without making those modules executable.
