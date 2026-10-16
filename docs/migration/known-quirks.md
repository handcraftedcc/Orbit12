# Known source quirks preserved in the port (fix later, deliberately)

The C++ port matches the Python source, including these bugs/oddities. Each is covered by a source-derived test, so a future fix must change the test and be a conscious behavior change. Do not fix silently.

## Strum (`modules/strum.py` -> `quantize_strum.cpp`)
- ARTC=EVEN resolves to `STRUM_ARTICULATE_PATTERNS[-3]` (the BNC pattern), not even spacing. The index is `articulate-3` over the list `EVEN,RNDLTE,RNDSTR,ACCL,DECL,HUMN,RAKE,SWNG,FLAM` (values 0..8), so HUMN resolves to the RAKE pattern and RAKE to the BNC pattern (one pattern early); SWNG and FLAM resolve to their own patterns, and ACCL/DECL are correct.
- CNT is inert: the source passes `STRUM_CHORD_LENGTH=5` to tilt, so CNT does not limit tilt as the docs claim.
- AMT=0 emits one note per pass, not simultaneous notes.
- `stop()` clears only the register; strum_active/schedule/count persist and end on the next empty-register pass.
- Pending-array eviction and note-delay-full drops are silent in the source (kept, counted, never stuck).

## IN slot octave/PADOFS (`core/_modules/_input.py`, `Orion.py`, `core/scenes.py`)
- Navigation keys change global `octave`/`key_offset` at once (`Orion.py:674-679`), but mirror them into the IN module's OCT/PADOFS parm rows only while IN is the active chain (`Orion.py:682-687`). A scene save writes the `S` globals and then `P,octave`/`P,key_offset` from the IN parm rows; load applies `S` first and the `P` records afterward through their callbacks (`scenes.py:267-298`). A scene saved after changing OCT/PADOFS while another slot was active can therefore restore the stale IN values. Fixed in C++ (intentional deviation, scene-affecting only): `Controller::nav_notes_adjust` (`controller.cpp:1158-1169`) always mirrors the global octave/key_offset into the IN rows, so a scene saved after changing them with another slot active holds the current values. Covered by `t11b_scenes` `octave_offset_round_trip_with_other_slot_active`.
- PADOFS has two units. Scale layouts shift by scale degrees and carry into octaves (`_input.py:116-124`), while the chromatic note layout adds it as semitones (`inputmodules/note.py:63`). The same PADOFS value therefore moves pads by different intervals per layout. Preserved as-is.

## Not preserved: forced by "never leave a stuck note" (see status.md)
Same-pass on-then-off ordering (output stage), gate/chopper/retrigger/arp/latch/quantize stuck-note cases, strum same-batch re-press. These are intentional deviations, not quirks.
