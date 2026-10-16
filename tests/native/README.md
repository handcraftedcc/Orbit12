# Native test harness and fixtures

Configure this directory with CMake and run its registered tests through CTest.
The default build is C++17 with warnings treated as errors. `ORBIT_NATIVE_ASAN_UBSAN`
enables AddressSanitizer and UndefinedBehaviorSanitizer for Linux host runs.
`ORBIT_NATIVE_TSAN` is reserved for a distinct bridge-only build after the
cross-core bridge implementation exists; it must not be combined with ASan.

Shared fakes in `fakes.hpp` provide fixed-capacity clock samples and random
choices. An empty clock returns zero; after the recorded clock samples are
exhausted, the last sample is held. An empty random stream returns zero; after
recorded choices are exhausted, the last choice is held. Both reject writes
past capacity. MIDI and storage fakes refuse every operation and count attempts;
these deliberately minimal refusal-shaped helpers do not claim conformance to
future production port signatures. `t02_fakes` checks these semantics,
including that failed reads do not mutate the destination.

Musical behavior is checked with generated C++ trace fixtures, not JSON Lines.
Each `.inc` or `.hpp` file under `fixtures/` is generated; do not edit it by hand.

| Fixture | Generator | Used by |
| --- | --- | --- |
| `fixtures/t09d_source_traces.inc` | `fixtures/t09d/gen_traces.py` (`python -I`) | `t09d_gate_chopper_retrigger.cpp` |
| `fixtures/t09e_source_traces.inc` | `fixtures/t09e/gen_traces.py` (`python -I`) | `t09e_arp.cpp` |
| `fixtures/t09f_source_traces.inc` | `fixtures/t09f/gen_traces.py` (`python -I`) | `t09f_arp_walk.cpp` |
| `fixtures/t09g_source_traces.inc` | `fixtures/t09g/gen_traces.py` (`python -I`) | `t09g_euclid.cpp` (also holds `kEuclidStepTable`) |
| `fixtures/t09h_source_traces.inc` | `fixtures/t09h/gen_traces.py` (`python -I`) | `t09h_latch_pick.cpp` |
| `fixtures/t09i_source_traces.inc` | `fixtures/t09i/gen_traces.py` (`python -I`) | `t09i_quantize_strum.cpp` |
| `fixtures/t09j_source_traces.inc` | `fixtures/t09j/gen_traces.py` (`python -I`) | `t09j_wander.cpp` |
| `fixtures/t11b_traces.inc` | `tools/arduino/oracle/t11b_harness.py` (full `Orion.run` loop) | `t11b_scenes.cpp`, `t11b_shortcuts.cpp` |
| `fixtures/t11b_model_tables.inc` | `tools/arduino/oracle/t11b_harness.py --emit-model` | `t11b_ui_model.cpp` |
| `fixtures/t03_golden.hpp` | `tools/arduino/generate_music_fixture.py` (from `core/music.py`) | `t03_music.cpp` (checksums) |

Each `t09*` generator runs the Python source under `python -I` (its `shims/`
directory stubs `adafruit_ticks` where needed) and writes one C++ function per
trace. A trace function replays ordered steps: `Run::set_global`/`set_int`/`set_float`
set parameters and globals, and `Run::step(now, tick, running, on, off, ...)`
supplies the clock sample, tick, transport state, and input note-ons/offs for one
pass. The source's note-ons/offs for that pass are compared in order. Functions named
`trace_dev_*` (or driven through `step_dev`) document a deliberate deviation: the
C++ result is checked and must differ from the recorded source result.

Expected output comes only from the Python source oracle (or an independently
reviewed derivation), never from the C++ implementation under test. Cite the
source function in the generator and keep each trace small enough to audit.
Add or regenerate a trace only when the behavior it covers is implemented and
the generator has been run.
