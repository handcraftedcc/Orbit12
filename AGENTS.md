# Orbit12 Arduino migration

## Ownership and scope
- The user delegates architecture to the lead, implementation coordination/integration to Sol (`gpt-6.1-sol`), and ordinary implementation to Luna (`gpt-6-luna`). Explicitly choose those models when spawning agents with fresh, narrowly scoped context. Do not spawn another architect for routine work.
- Read `docs/migration/architecture.md`, `contracts.md`, and your task in `plan.md`. Read only the relevant behavior/discovery sections and Python modules afterward. Source under `circuitpython-source/src/` is the behavioral oracle; resolve discrepancies in discovery notes against source and record the correction.
- Sol owns dependency pins, integration, compiler errors, tests, status, and task allocation. Luna owns assigned files and acceptance checks. Avoid overlapping writes. Consolidate architectural escalations using `decisions.md`.
- The user explicitly authorizes moving the CircuitPython implementation, generated `circuitpython-source/src_mpy/`, tools/docs/assets and supporting files into `circuitpython-source/` for a clean root. T-01 preserves their contents and repairs path references/build entrypoints; do not regenerate Python deployment artifacts or remove user data. New firmware lives in `firmware/`; host tests in `tests/native/`; migration tools in `tools/arduino/`.

## Implementation rules
- C++17, core 0 owns musical/controller state and USB, core 1 owns physical inputs and rendering, no RTOS. Use only the bounded bridge in `contracts.md`; never share mutable module/UI state. No heap allocation, Arduino String, STL dynamic containers, RTTI, exceptions, filesystem writes, or blocking waits in the performance loop. Library initialization allocation is allowed and must be measured.
- Use fixed arrays and explicit capacities; report overflow. Keep Arduino/library includes in platform adapters. Keep domain logic host-compilable. No generic event bus, service locator, or inheritance hierarchy of module subclasses.
- Use zero-based MIDI channels in domain code; convert exactly once at the platform boundary. Use wrap-safe time helpers, bounded work, and explicit integer widths.
- Implement source defaults, parameter names, ranges, callbacks, saved attributes, routing, reset/stop behavior, and ordering. Do not replace complex modules with approximations or silent stubs. Hide the unimplemented Python `phraser` entry.
- Protect note releases under pressure. No silently lost note-off or fabricated success after storage/USB failure. Diagnostics are counters and bounded records, never MIDI-path serial logging.
- Do not change public contracts, capacities, scene semantics, or dependency families unilaterally. Sol may resolve local implementation details and source ambiguities supported by evidence; escalate architectural changes once with alternatives.
- Test behavior and invariants, not copies of implementation. Every implemented module needs deterministic source-backed fixtures. Record tests actually run separately from tests planned or hardware checks pending.
- No hardware flashing, filesystem formatting, global Git configuration changes, publication, or removal of user data without authorization. Toolchain installation into workspace-local directories is allowed; pin and document it, obey environment approval requirements.

## Handoff
Each task reports changed files, contract compliance, commands/results, unverified hardware behavior, and blockers in a concise entry in `docs/migration/status.md` (coordinator owns that file). Do not declare the migration complete until the release gates in `testing.md` pass. Routine failures go to Sol, not the architect.

The coordinator must continue allocating ready dependency-ordered tasks after each batch, rather than stopping at a batch boundary. If work stops, report explicitly what completed, why it stopped, and the exact next action needed from the user. A hardware gate does not block independent host-side work. Never leave a status claiming agents are running after they have finished. A five-minute thread heartbeat monitors stopped work and meaningful blockers; avoid duplicate unchanged notifications.
