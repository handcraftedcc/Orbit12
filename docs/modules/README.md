# Writing Orbit12 modules

Modules are native C++ processors compiled into the firmware. This guide covers how they are laid out,
what a manifest says, how a fork builds and installs its own, and which rules keep core and user code
separate. The generator is `tools/modules/gen_registry.py`; the scaffold is `tools/modules/new_module.py`.

## Two kinds of module

| | Core (shipped) | User (third party) |
|---|---|---|
| Location | `firmware/Orbit12/src/modules/core/<id>/` | `firmware/Orbit12/src/modules/user/<author>/<name>/` |
| `module_id` | the plain id, e.g. `arp` | `user.<author>.<name>`, lowercase `[a-z0-9_]` |
| `scene_key` | equal to `module_id` (scenes depend on it) | any `[a-z0-9_]`, at most 10 characters, unique |
| Compiled | always | when present in the tree and `enabled_in_release` is not `false` |
| Input modules | six core inputs; user inputs allowed (see below) | `kind: input`, with a state header |

Shared helpers and the state union live in `firmware/Orbit12/src/modules/common/`. Test code and
sample modules are never under `firmware/Orbit12/src/`: arduino-cli compiles every `.c`, `.cpp` and `.S`
under the sketch `src/` folder, recursively (verified with arduino-cli 1.5.1: the pinned build compiles
`src/modules/core/**`).

## Layout of a module directory

```
firmware/Orbit12/src/modules/user/alice/octave_up/
  module.json        manifest (below)
  octave_up.hpp      declares the descriptor accessor
  octave_up.cpp      implementation
```

Include shared headers with a path relative to the module's own file. From
`firmware/Orbit12/src/modules/user/<author>/<name>/` the shared state header is
`#include "../../../common/module_state.hpp"`. Root-relative paths such as `"modules/common/module_state.hpp"`
do not resolve in the Arduino sketch build (checked with the pinned arduino-cli 1.5.1: the sketch compile
failed with "No such file or directory"); they only work in the native tests, which add `firmware/Orbit12/src`
to the include path. A module moved between `src/` and `modules/examples/` must fix its include paths.

Start from the scaffold:

```
python -I tools/modules/new_module.py new --kind processor --author alice --name octave_up
```

It writes a compiling pass-through stub and regenerates the tables. The stub ships in the firmware
until you change or remove it.

## The manifest (`module.json`)

| Field | Rule |
|---|---|
| `package_format` | `1` |
| `module_id` | core: equals the directory name. user: `user.<author>.<name>` matching the directory |
| `scene_key` | `[a-z0-9_]{1,10}`, unique across core and user modules. Core: equals `module_id` |
| `display_name` | free text, for documentation |
| `label` | selector label, `[A-Z0-9]+`, at most 7 characters (the widest shipped label) |
| `kind` | `processor` (chain slot) or `input` (rejected for user modules; see below) |
| `module_version` | integer 1..65535 |
| `engine_api` | must equal `kEngineApi` in `app/build_info.hpp` (currently `1`) |
| `descriptor` | footer text, at most 17 characters, only characters the font draws (`kFontChars` in `app/renderer.cpp`: upper case, digits, space and punctuation) |
| `state` | `{ "type": "<C++ state struct>", "bytes": N }`. `bytes` is at most 21,845 (128 KiB chain budget / 6 slots) |
| `parameters` | list; see below |
| `attributes` | saved attribute arrays (`AttributeVisitor`), at most 8 per module |
| `header` | the header that declares `<accessor>()` |
| `sources` | `.cpp` files, relative to the module directory |
| `provenance` | `{ "origin", "author", "license" }`; `author` must equal the `module_id` author; `license` is required |
| `enabled_in_release` | `false` keeps a user module out of the release (default `true`) |

Each parameter:

| Field | Meaning |
|---|---|
| `name` | `[a-z][a-z0-9_]*`, unique, at most 21 characters (saved field name) |
| `type` | `integer`, `decimal`, `enumeration`, `action`, `boolean` or `text` |
| `default`, `minimum`, `maximum`, `step` | integers (int32) or finite numbers; booleans must be JSON `true`/`false`; `minimum <= default <= maximum` |
| `options` | enumeration labels |
| `label` | display label, `[A-Z0-9]+` |
| `saved` | persisted to scenes |
| `ui_only` | never persisted (action buttons) |
| `global_binding` | `none`, `octave` or `key_offset` |

Parameter counts are checked against the SCN1 slot bounds. A processor may have at most 12 persisted
parameters, because the generator adds the three inherited controls (`source_mode`, `operation_mode`,
`out_channel`), and the slot bound is 15. Attributes allow up to 3 rows per processor.

## The implementation contract

- Zero-based MIDI channels in domain code. No heap, no `String`, no STL containers, no logging, no
  blocking waits, no exceptions and no RTTI in the performance path.
- Every note-on must have a tracked release. A release must match its note-on's pitch and channel.
- `process()` clears the output first. A full output sets the sticky `overflowed()` flag, which the
  chain turns into a fault (all notes off). Never clear that flag or drop items silently.
- A module with state declares a state struct. Core structs live in `modules/common/module_state.hpp` and
  `input_state.hpp`; a user struct lives in its own `state_header` (only plain fields and `domain/types.hpp`
  includes). The union of states (`modules/generated/module_state_generated.hpp`, `input_state_generated.hpp`)
  is generated from the manifests, with a `sizeof` check against `state.bytes`. The sample processor
  `modules/examples/example/octave_shift` is stateless and reuses `EmptyModuleState`.
- Use `ProcessContext.now` with the wrap-safe helpers in `domain/time.hpp`.

## Core vs user separation rules

1. Core ids are reserved. A user module cannot use an unprefixed id, and the generator checks the
   directory name against the id.
2. Core labels are not reused. The one allowed shared label is `CHRD` (the core processor `chord` and the
   core input `chords`); the generator rejects any other repeat.
3. `modules/core/`, `modules/common/` and `modules/generated/` belong to the maintainers. Do not edit the
   generated files; `gen_registry.py` rewrites them. A fork may change core files in its own repository.
4. Test code for a user module lives under `tests/native/` (or a fork's test directory), never in
   `firmware/Orbit12/src/`.

## Input modules

User input modules are supported (decisions.md, ADR-12). Input `kind` modules use the same manifest; the
generator derives the scene bank layout from every input in the build (core and user, selector order):

- Each input has one SCN1 bank section (`BANK,<scene_key>`) in every scene file. Its size is
  (1 BANK header + persisted parameters + 2 inherited slots + attribute rows) lines, and its values add to the
  bank region. A scene with an extra input is therefore larger: the generator prints the new bound.
- Budgets (checked by `gen_registry.py`): the scene file bound must keep 16 scenes plus `.tmp` and `.bak`
  within 70 % of the 1 MiB partition (`SCENE_FILE_BUDGET` = 40,777 bytes per file), and the SceneDto bank values
  must fit `DTO_BUDGET_BYTES` (16 KiB). The chain state budget and the release flash and RAM budgets apply as before.
- Scaffold: `python -I tools/modules/new_module.py new --kind input --author A --name N` writes a state
  struct in `N_state.hpp`, which the generated input state union includes.

## Scene files and unknown modules

A scene stores each module's `scene_key` (never the `module_id`). The controller maps the key to the module
id on load and back on save (`module_id_for_scene_key` / `module_scene_key`, generated in
`modules/generated/registry_generated.cpp`). A key the firmware does not know is handled like this
(`persistence/scene_codec.cpp`, `app/controller.cpp`):

- The scene still loads. The row is flagged `SceneUnknownModule` in the scene diagnostics (I did not find
  an on-screen message for the flag).
- A processor slot installs `empty`; the input slot installs `note`.
- The module's parameter and attribute lines are skipped.
- Saving that scene on the firmware without the module writes `empty` (or `note`) in that slot. The user
  module's saved values are lost in that save.

So a scene that uses a user module should be saved only on firmware that has the module. Reinstalling the
module does not restore values lost by a save without it. This is the existing policy and is unchanged
here; a change to it needs a decision.

## Build and install a fork

Forks use the same release workflow as the official repository (`.github/workflows/release.yml`):

1. Fork `handcraftedcc/Orbit12`, add your module directory under `firmware/Orbit12/src/modules/user/`, and
   run the generator and the tests:

   ```
   python -I tools/modules/gen_registry.py --check
   python -I -m unittest discover -s tools/modules/tests -v
   powershell -NoProfile -File tools/arduino/test-native.ps1 -BuildDirectory build/native -Configuration Release
   ```

2. Tag a semver version (for example `v0.1.0-pre.1`). The workflow builds and publishes the firmware
   image, `release-manifest.json` and `SHA256SUMS` to your fork's GitHub release.
3. Install from your fork with the installer:

   ```
   orbit12-installer --repo <you>/<fork> info
   orbit12-installer --repo <you>/<fork> flash
   ```

   The installer downloads only from the repository named by `--repo` (default `handcraftedcc/Orbit12`).

Trust: a release built from a fork runs code from that fork. The release is not signed yet. Check the
`SHA256SUMS` file, read the module sources you install, and install only from repositories you trust.

## Budgets and what the generator checks

`gen_registry.py` fails when any of these would be exceeded:

- the SCN1 scene bound (`kSceneMaxBytes`, 38,637 bytes), derived from the manifests;
- the chain state budget (6 × the largest state must fit 128 KiB);
- the per-module limits above, label and descriptor rules, and scene key uniqueness.

Flash and RAM are checked by `tools/arduino/build-firmware.sh` against `ORBIT_FLASH_BUDGET` (409,600 B)
and `ORBIT_RAM_BUDGET` (112,640 B), the same numbers as the release workflow. Build the firmware to see the
impact of a module: `bash tools/arduino/build-firmware.sh`.

## Known limits

- Runtime loading is out of scope (decision deferred). Modules are compiled in.
- The state union is generated, but the state struct of a core module stays in `modules/common`.
- Native tests run the shipped core tables (`ORBIT_NATIVE_CORE_ONLY`). The state union and the input bank layout
  include every module compiled into the tree. A user module is tested through the test-profile registry
  (`tests/native/t17_user_example.cpp`); the oracle traces stay core-only by design.
- No controller-level scene round-trip test exists yet for user modules (see the report for the test seam).
- The generator does not measure flash or RAM. The firmware build does.
- arduino-cli copies only C/C++ sources and headers (`.c .cpp .h .hpp` and similar) into its build sketch. An
  include of a file with another extension (such as `.inc`) fails in the sketch build even though it works natively.
  Generated fragments therefore use `.hpp`.
