# Architectural decisions and escalation

| ID | Decision | Reason / validation |
|---|---|---|
| ADR-01 | Keep existing board; Arduino-Pico with one selected TinyUSB stack | Official MacroPad Arduino support; T00 confirms exact pin/dependency/USB configuration. |
| ADR-02 | Fixed in-place module state and descriptors, explicit ownership | Removes imports/GC/heap churn without translating the Python helper graph; compile size assertions include repeated heavy slots. |
| ADR-03 | Preserve source musical semantics and valid SCN1 files | Existing users need matching instruments and scenes; golden traces establish parity. Hide absent phraser, validate malformed input safely. |
| ADR-04 | LittleFS plus stopped CDC import/export maintenance | Retains editable scene interchange without unsafe shared USB MSC/filesystem ownership. Boot workflow changes are documented. |
| ADR-05 | Bounded queues with release-safe overload recovery | Original fixed arrays sometimes silently drop events; correct note lifetime takes priority under overload. |
| ADR-06 | Dedicated music/USB/controller core 0; input/render worker core 1, gated by T00/T11a | User requested MIDI/UI isolation. Official Arduino-Pico documentation supports setup1/loop1 and core-0 USB ownership. Use ordered input SPSC and copied UI snapshot queue; no shared mutable state. Shared flash writes remain stopped-only. Timing improvement and target atomics require device/compiler evidence; single-core is an escalation alternative if feasibility fails. |
| ADR-07 | Pinned Arduino CLI + host CMake tests | Simple target build, fast host verification, no second embedded dependency framework. |
| ADR-08 | Reorganize existing CircuitPython sources under `circuitpython-source/` | User authorized a clean Arduino base while preserving CP source, deployment artifacts, UI assets and tools. T-01 verifies the move and path updates. |
| ADR-09 | Native manifest/registry baseline; runtime module loading remains an E00 evidence gate | Adding algorithms should be straightforward. The user prefers independent installation without firmware relink if target measurements support it. Evaluate relocated native and established small bytecode prototypes against native transpose/delay before deciding; installer-managed firmware updates remain the supported fallback. See `extensions.md`. |
| ADR-10 | Exact existing UI assets, layout and user-facing behavior are required | Preserve bitmap/font geometry and all navigation and display content; extension management belongs in the installer, with no device UI redesign. |

Sol resolves compiler errors, local algorithm interpretation against source, dependency patch pins within approved families, test harness adjustments, and disjoint file integration. Do not ask the architect to inspect routine modules or repeat discovery. Do not ask the user to choose implementation minutiae.

Escalate only: fundamental target/USB/dual-core assumption fails; fixed memory budget requires dropping features or a new layout; shared contracts/scene format need incompatible changes; measured timing requires architectural redesign; or a scheduled checkpoint is reached. Batch independent issues into one report, <=500 words plus necessary evidence:

```
Checkpoint / affected ADR:
Observed evidence (command, source or device measurement):
Invalidated assumption and user-visible impact:
Option A (recommended): change, benefit, cost, compatibility/test impact
Option B: same comparison
Decision needed and unaffected tasks continuing:
```

No unsupported performance assertions, silent simplifications, or repeated broad questions. Record accepted changes here and update affected contracts/tasks before handing them to Luna. If tools/hardware are missing, report the exact blocked verification and continue independent work; a missing tool is normally a coordination issue, not an architecture escalation.
