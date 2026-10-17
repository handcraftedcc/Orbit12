# Releasing Orbit12 firmware

Releases are built by `.github/workflows/release.yml`. The same steps run locally through
`tools/arduino/build-firmware.sh`, so a release can be checked before tagging.

## What a release contains

| Asset | Produced by | Notes |
| --- | --- | --- |
| `Orbit12-<version>.uf2` | `build-firmware.sh` | RP2040 image, family `0xe48bff56`. |
| `release-manifest.json` | `make_release_manifest.py` (called by the build script) | Schema 1, read by the installer. Field names are fixed. |
| `SHA256SUMS` | publish job | SHA-256 of every release asset. |
| `orbit12-installer-windows.exe`, `orbit12-installer-macos`, `orbit12-installer-linux` | installer job | Only when `tools/installer/build_exe.py` exists; otherwise skipped. |

The firmware build fails when the flash image exceeds 400 KiB (`ORBIT_FLASH_BUDGET=409600`) or the
global RAM exceeds 110 KiB (`ORBIT_RAM_BUDGET=112640`). The current build uses about 242 KiB flash
and 62 KiB globals. The ELF must contain the four `__wrap_` USB guard symbols, or the build fails.

The manifest records the image address range from the UF2 blocks, and the LittleFS partition from
the linker symbols `_FS_start`/`_FS_end` in the ELF. It refuses an image that overlaps the
filesystem, an unstamped ELF, a non-semver version, and a dirty git tree.

## Versions and prereleases

- A tag `vX.Y.Z` or `vX.Y.Z-suffix` builds version `X.Y.Z` or `X.Y.Z-suffix`.
- A tag containing `-` (for example `v0.1.0-pre.1`) is published as a GitHub **prerelease**.
- Until the project drops the suffix, every release is a prerelease. Tags without `-` publish a normal
  release.
- Tags must be semver. A non-semver tag builds, but the manifest step fails and the release job stops.

## Cutting a release

1. Confirm the release commit on `main` (or the release branch) passes the native tests and the
   `build-firmware.sh` run described below, with a clean working tree.
2. Create and push an annotated tag: `git tag -a v0.1.0-pre.1 -m "Orbit12 0.1.0-pre.1"` then
   `git push origin v0.1.0-pre.1`. Pushing the tag starts the workflow.
3. The `test` and `firmware` jobs run. The `installer` matrix runs when its script exists.
4. The `publish` job runs only for a tag push, after all jobs succeed. It creates the GitHub release
   and attaches the assets listed above.

Do not move or re-push a tag after it has been published. Cut a new version instead.

## Manual builds (workflow_dispatch)

Run the workflow from the Actions tab with an optional `version` input (default
`0.0.0-ci.<run number>`). It uploads artifacts named `orbit12-firmware-<version>` and
`orbit12-installer-<os>`. It never publishes, even when run on a tag ref.

## Module tables and user modules

The module registry, footer descriptions, module inventory and scene bounds are generated from `module.json` files (`tools/modules/gen_registry.py`). CI runs `gen_registry.py --check` and the generator unit tests in both the native and the release workflows; a stale generated file fails the build. Release modules are the core set plus each user module under `firmware/Orbit12/src/modules/user/` whose manifest does not set `enabled_in_release: false`. Sample modules under `modules/examples/` are never built into the firmware. The release manifest's `modules` field lists core and user ids.

## Verifying locally

Run the same steps the workflow runs. Use a semver version to get the manifest.

```bash
# Native tests (Windows PowerShell; Linux/macOS: the cmake commands in .github/workflows/release.yml)
powershell -NoProfile -File tools/arduino/test-native.ps1 -BuildDirectory build/native-rel -Configuration Release

# Manifest and ELF helper unit tests
python -m unittest discover -s tools/arduino/tests -v

# Module generator: stale check and unit tests
python -I tools/modules/gen_registry.py --check
python -I -m unittest discover -s tools/modules/tests -v

# Toolchain (Linux/macOS or Git Bash); Windows users may run tools/arduino/bootstrap.ps1
bash tools/arduino/bootstrap.sh

# Firmware for a release version (writes .tool-cache/artifacts/release/dist/)
bash tools/arduino/build-firmware.sh --version 0.1.0-pre.1

# Manifest only, from an existing build (add --allow-dirty when the tree has local changes)
python tools/arduino/make_release_manifest.py --help
```

`build-firmware.sh` options: `--version V` (or `ORBIT_VERSION`), `--upload PORT` (explicit flashing
only), and the environment variables `ORBIT_ARDUINO_CLI`, `ORBIT_ARDUINO_CONFIG`, `ORBIT_PYTHON`,
`ORBIT_FLASH_BUDGET`, `ORBIT_RAM_BUDGET`, `ORBIT_REPO`. Without `--version` the version comes from
`git describe --tags --always --dirty`, and no manifest is produced for a non-semver result.

The identity header `firmware/Orbit12/src/app/build_info.hpp` is stamped during the build and
restored to its committed placeholder afterwards. A stamped build reports the version on the `?`
serial dump, in the line `VERSION fw=... git=... engine_api=... board=...`.

## Installer for users (what to tell them)

The installer is one program per platform. Users download it, double-click it and follow the window. The
plain-language guide and the warnings they may see (Windows SmartScreen "More info > Run anyway", macOS
right-click > Open, Linux `chmod +x` and the `dialout` group) are in `tools/installer/README.md`, section
"Plain-language guide". The executables are unsigned, so those warnings are expected until signing exists.

Windows and macOS builds are windowed (no console window). On Windows, running the program with arguments
prints to the terminal that started it (checked on Windows only). Linux keeps a console build. The macOS
windowed build is not verified yet; see the open items in `tools/installer/README.md`.

## Forks

The manifest's `repo` field defaults to `handcraftedcc/Orbit12`. Forks set `ORBIT_REPO` for local
builds, and the workflow uses the repository it runs in. The installer downloads only from the
configured repository (default `handcraftedcc/Orbit12`, or `--repo` for a fork). There is no hosted
registry.

## Not verified by these scripts

- Device behavior of the released image (flashing, USB MIDI, display, storage) is a hardware check,
  done separately.
- The Linux CI runner's arduino-cli bootstrap is verified by the workflow's first run, not locally.
- The installer job depends on the installer worker's `tools/installer/build_exe.py` contract:
  it must write exactly one executable into `$ORBIT_INSTALLER_OUT`.
