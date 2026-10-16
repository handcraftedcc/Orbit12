# Automated build and verification strategy

T00 supplies workspace-local, version-pinned Arduino CLI bootstrap and `sketch.yaml`; scripts must print exact version/FQBN/options and preserve dependency logs. Host CMake/Ninja and compiler are provisioned explicitly when absent. CI runs on a clean Linux runner; local PowerShell wrapper uses the same commands/configuration. No board or toolchain is presently verified by the architecture documents.

Required CI jobs:
1. Configure/build/run native C++17 domain tests with warnings enabled and warnings-as-errors for project code. Add AddressSanitizer/UndefinedBehaviorSanitizer on supported Linux host builds; do not force these on the firmware toolchain.
2. Run source-backed golden fixtures, codec malformed-input corpus, deterministic clock/random cases, and multicore bridge stress tests. Use ThreadSanitizer for the host bridge where supported, in a separate job from ASan.
3. Compile the pinned Arduino MacroPad profile from a clean dependency cache; emit ELF/UF2/map/size report and dependency manifest as artifacts. Enforce RAM budgets from architecture and partition flash capacity. Do not count host-only tests as target compilation.
4. Verify all registry entries have a real implementation, parameter/persistence schema and fixtures; generated musical tables/assets reproduce; no phraser or placeholder processors are exposed.
5. Reproduce every existing runtime bitmap/font byte and geometry from `circuitpython-source/`, compare every screen layout and navigation state against source captures, and reject substitutions that alter visible content.
6. Run E01 manifest/registry generation and example-module tests, and E02 installer mocks for board discovery, backups, hash/ABI/partition checks, interrupted transfers and re-enumeration. E00 records target measurements or remains explicitly unverified; host-only benchmarks cannot choose the loader architecture.

Golden tests store input key/MIDI/timestamp/random events and expected ordered output events and snapshots. Record source file/function and expected-value derivation. Prefer a small CPython harness around pure Python modules with faked hardware/time/random to generate differential traces; when CPython cannot run a source module, derive and independently review short traces, documenting the limitation. Never generate expected values from the C++ code under test. Python and C++ random algorithms need not produce identical seeded streams; inject the same bounded random choices for parity tests.

Coverage matrix: all inputs/processors; all five routing operations and valid source taps; channels 0/15 and same pitch across channels; polyphony boundaries; delayed events across timer rollover; 1/120/300 BPM and swing extremes; internal/external start/stop/continue; queued external traffic; 10 scenes and legacy slot layout; save/copy/load/reset failures; every shortcut; parameter callback side effects; key releases during UI load, queue saturation, scene epochs and USB backpressure. Source ordinary overflow behavior is documented; release safety is the deliberate exception.

Cross-core tests: one writer/one reader per queue, randomized scheduling, publication ordering, index wrap, queue full, snapshot reader stalled, stale edit command after scene switch, encoder flood with key release, startup-ready ordering, stop/maintenance acknowledgement, and forced 100 ms renderer stall. Never assert hardware timing from desktop threads.

Hardware release procedure (requires actual MacroPad and host MIDI capture):
- Confirm pins, encoder detents/sign, twelve keys/pixels, SH1106 geometry/sleep, USB enumeration and channel conversion.
- Measure clock and note latency under idle and continuous encoder/key/menu/display activity; test heaviest six-slot chain at 300 BPM/full swing. Engineering target: internal clock error p99 <=0.5 ms and max <=1 ms over a 10-minute run; physical key-to-queued-MIDI p99 <=5 ms. Report USB host jitter separately from firmware GPIO instrumentation. These are proposed release targets, not existing app guarantees.
- Hold core1 rendering for 100 ms and verify core0 clock remains within the same bound; input responsiveness during that artificial stall may degrade and must not lose eventual key releases. Record worst loop/ISR/queue usage and errors.
- Disconnect/reconnect USB with held notes; saturation and recovery must leave no stuck notes. External source test measures received-clock-to-processing latency separately from upstream jitter.
- Measure static RAM, both core stack high-water marks and post-init free margin with heavy repeated modules and largest scene; 1,000 scene transitions show no growth. Test power loss during each persistence phase with recoverable previous state.
- Verify maintenance import/export and musical/UI parity on device. Preserve user's existing scene backups. Flash only when authorized.
- Confirm installer device identity and partition-safe UF2 update, then verify post-flash firmware/module inventory and scene preservation. Evaluate independent runtime module prototypes under the E00 target load where available.

Performance thresholds failing on real hardware go first to Sol for profiling and local correction; genuine scheduler/state-budget redesign goes through `decisions.md`. A green host suite or successful target compile does not waive hardware checks. Status labels: planned, implemented, host-verified, target-compiled, hardware-verified, blocked. Report only achieved levels.
