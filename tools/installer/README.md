# Orbit12 installer

One downloadable program that installs or updates Orbit12 firmware on the Adafruit MacroPad
RP2040. It downloads the latest published release from GitHub, checks it, backs up your scenes
and settings, puts the board into the bootloader, copies the firmware, and checks that the board
came back running it. No Python is needed on your computer.

Status: the host logic is covered by mock tests. It has **not** been run against a real board
from this package. See "Not verified on hardware" below before relying on it.

## For users

### Install or update (Windows, macOS, Linux)

1. Download `orbit12-installer-windows.exe`, `orbit12-installer-macos` or
   `orbit12-installer-linux` from the Orbit12 GitHub release page.
2. Connect the MacroPad with its USB cable. Nothing is written until you confirm.
3. Start the program. Double-click it, or run it from a terminal with no arguments.
4. Choose **Install the latest Orbit12 firmware**.

The program then:

1. Finds the newest **stable** release and shows its notes. (If only prereleases exist it says
   so and tells you to use `--include-prereleases`.)
2. Downloads the release files into a temporary folder and checks the SHA-256 hashes against
   the release manifest and `SHA256SUMS`, then checks the firmware file's structure. Anything
   that does not match is refused.
3. If Orbit12 is running, asks you to put the board in maintenance mode and saves your scenes
   and settings to a timestamped folder called `backup-YYYYMMDD-HHMMSS` in the `orbit12-backups`
   folder next to the program. The backup is written before anything is flashed.
4. Puts the board into the bootloader (USB drive `RPI-RP2`) and copies the firmware. For a board
that runs CircuitPython it can reset the board over USB; for other boards it asks you to hold the
BOOT button (see Adafruit's MacroPad RP2040 guide for where it is).
5. Waits for the board to restart and checks the firmware version it reports.

If the board's scene storage is not mounted afterwards (a board that was never formatted for
Orbit12, for example one that still runs CircuitPython), the program offers to format it. Formatting
erases every file on the partition, so it asks you to **type ERASE**. If you have a backup, it is
restored after the format.

Keep the backup folder until you have checked the scenes on the board.

### Other commands

Run `orbit12-installer --help`, or `orbit12-installer <command> --help`.

| Command | What it does |
|---|---|
| `install` | Download the newest stable release (or `--tag`), verify it and flash it. |
| `flash` | Same as `install`, but with `--tag` or offline files and fewer questions. |
| `releases` | List published releases and say which one `install` would pick. |
| `info` | Show the port, storage state and firmware version of a connected board. |
| `backup` | Save scenes and settings to a backup folder (needs maintenance mode). |
| `restore --from FOLDER` | Write a backup back to the board. Asks you to type ERASE first. |
| `format` | Erase and format the scene partition. Asks you to type ERASE. |

Useful options: `--port COM5` (choose the port), `--backup-dir FOLDER`, `--pause` (wait for Enter
before the window closes).

### Offline install

If you already have the files, nothing is downloaded:

    orbit12-installer flash --uf2 Orbit12-0.1.0-pre.1.uf2 --manifest release-manifest.json --sums SHA256SUMS

`--sums` is optional. The image and manifest are still checked.

### Forks and other repositories

`--repo owner/name` installs from another GitHub repository. The program then warns that the
firmware is **trusted code** that will be written to your board, and asks you to type the
repository name. `--yes` does not skip this question.

Checksums show that the downloaded files match each other. They do not show who published them.

### Entering maintenance mode

Maintenance mode lets the installer read and write your scenes. Today you enter it by hand:
unplug the board, hold any key while you plug it back in, keep it held for about two seconds,
release it, and press Enter in the installer. Maintenance mode ends when the installer sends
its exit (`EXIT-MAINT`; the plain `Q` byte on older firmware), or after 60 seconds without traffic.

When the firmware supports it (INFO answered), the installer enters maintenance itself with the
ENTER command, and leaves it with EXIT-MAINT. ENTER is accepted only while playback is stopped.
If playback is running, the installer asks you to stop it (hold the encoder and press the first
pad), then retries every 2 seconds for up to 20 seconds. If maintenance still cannot be entered,
or the firmware does not answer INFO (older firmware), the installer falls back to the hand-entry
steps above and says why.

### If something goes wrong

- **No RPI-RP2 drive appears.** Unplug the board. Hold the BOOT button (Adafruit's MacroPad RP2040
  guide shows where it is), plug the board in, and release BOOT when a drive named `RPI-RP2`
  appears. Run the installer again.
- **Several RPI-RP2 drives are connected.** Unplug the other boards. The installer never picks one.
- **Copy stopped part way.** The board is still in the bootloader. Run the installer again.
- **The board does not come back.** Unplug it and plug it back in. If it still does not respond,
  copy `Orbit12-<version>.uf2` onto the `RPI-RP2` drive by hand. Your backup folder is untouched.

The installer never formats the board's storage unless you type ERASE, and never chooses between
two drives or two boards.

## For developers

### Layout

- `orbit12_installer/cdc.py`: maintenance frame encoding, status-dump parsing, bounded client.
  The framing is vendored from `tools/arduino/scene_transfer.py`; `tests/installer/test_cdc.py`
  checks it against the repository copy when that file is present.
- `orbit12_installer/uf2.py`: UF2 block checks (magic, family, flags, sizes, address range,
  overlap with the filesystem partition).
- `orbit12_installer/package.py`: release manifest schema 1, `SHA256SUMS`, safe file names,
  whole-image verification.
- `orbit12_installer/github.py`: release list, stable/prerelease selection, urllib downloads
  with an https host allowlist, redirect checks, streamed size limits, and `GITHUB_TOKEN` sent
  only to `api.github.com`.
- `orbit12_installer/interfaces.py`: the seams: `SerialPort`, `PortEnumerator`, `SerialOpener`,
  `VolumeFinder`, `Clock`, `Downloader`, `Console`.
- `orbit12_installer/hardware.py`: real implementations (pyserial, removable volumes, clock).
- `orbit12_installer/device.py`: discovery, maintenance, backup, restore, format, BOOTSEL,
  copy and verification.
- `orbit12_installer/flow.py`: the install, repair, backup, restore, format and info flows.
- `orbit12_installer/cli.py`, `console.py`: command line, wizard, terminal prompts.
- `entry.py`: PyInstaller entry script. `build_exe.py`: builds the one-file program.

### Run the tests

No hardware, network or real sleeps are used. From the repository root:

    python -I -m unittest discover -s tests/installer -p "test_*.py"

The tests use fake ports, volumes, clock and downloader (`tests/installer/helpers.py`). The fake
board implements the behaviour documented in `docs/migration/storage.md`.

### Build the program

    pip install pyinstaller pyserial
    python tools/installer/build_exe.py

Output: `build/installer/dist/orbit12-installer-<windows|macos|linux>[.exe]`. Work and spec files
stay under `build/installer/`, which git ignores. The script runs `--version` on the result as a
smoke test (`--no-smoke` skips it). `--print-command` shows the PyInstaller command. Nothing is
published by this script. CI should run it on each platform runner.

Run without building: `python tools/installer/entry.py <command>`, or from `tools/installer`,
`python -m orbit12_installer <command>`.

### Design rules

- Destructive steps need the typed word `ERASE`. `--yes` answers only non-destructive questions.
  The non-default repository confirmation needs the repository name typed.
- Flash layout is checked against the RP2040 XIP window (0x10000000 to 0x11000000), not against an
  assumed flash size. The image window must lie below the filesystem window: image end <= filesystem
  start. The filesystem start and size must be 4096-byte aligned and inside the window, and every
  UF2 block must stay inside the image window and off the filesystem. The real clean build
  (image 0x10000000..0x10043000, 1072 blocks; filesystem 0x106ff000 size 0x100000) passes.
- Nothing is written to the board before the backup, the manifest and image checks, and the UF2
  checks have passed. A UF2 block may not touch the scene partition.
- A backup folder without `backup.json` is incomplete and is never restored. Restore verifies
  every file's size and SHA-256 first.
- The device identity comes from the INFO reply (`INFO fw= sha= api= board=`, plus `STORAGE` and
  `STATE` lines), which works in performance mode and in maintenance. The `?` status dump is the
  fallback for older firmware. Vendor IDs `0x239A` and `0x2E8A` are only the candidates scanned;
  the device is identified by its replies, not by USB PID.
- Maintenance entry and exit use the frame layouts from `docs/migration/storage.md` ("For the
  installer"), reproduced in `cdc.py` and checked against `tools/arduino/scene_transfer.py` by the
  drift tests. INFO is `I`, ENTER is `M` with name `ENTER-MAINT`, EXIT is `E` with name
  `EXIT-MAINT`. Replies are `D` (INFO) and `A`/`N` (ENTER, EXIT). A NAK carries a reason byte
  (`cdc.NAK_REASONS`).
- After the flash, the new firmware is verified from INFO: board, `engine_api`, `fw` version and
  `sha` (a `-dirty` suffix is a warning). `STORAGE mounted` from INFO decides whether the guided
  format is offered.
- `VERSION` from the dump is only read when INFO is missing. Its format is
  `VERSION fw=<version> git=<sha> engine_api=<n> board=<id>`.
- Playback check: ENTER's NotStopped (16) and Busy (5) are retried. Other NAKs fall back.

### Not verified on hardware

- The 1200-baud touch that enters the bootloader (Orbit12 and CircuitPython CDC ports). It is written as an open-at-1200 with DTR and
  RTS cleared, then closed; the firmware does not implement a touch handler of its own.
- Drive detection on each OS (Windows `GetDriveTypeW` removable check, macOS `/Volumes`, Linux
  `/media`, `/run/media`, `/mnt`) and whether the bootloader volume vanishes before close or fsync
  returns.
- Maintenance entry by holding a key, the backup and restore over a real serial link, and the
  format on a real partition.
- Host entry, INFO, ENTER and EXIT-MAINT, on real firmware. All of it is exercised only against the
  mock board, which follows the frame layouts documented in `storage.md`.
- Re-enumeration timing after a flash.
- The frozen program on Windows, macOS and Linux beyond `--version`, and the exact USB PIDs.

### Open questions

- The `MODULES` line's id separator is assumed to be a comma (`core=transpose,chords`). Confirm
  this with Package D when the firmware is built.
- The retry window (every 2 s, 20 s in total) and the wording of the stop-playback prompt are the
  installer's choice; confirm them on hardware.
- Code signing: the executables are unsigned, so Windows SmartScreen and macOS Gatekeeper will
  warn on first run.
