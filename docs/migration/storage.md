# Storage, scene lifecycle and CDC maintenance (T13)

Status: host-verified (t07b, t11b, t13*, t14_slice) and target-compiled. Hardware behavior
(flash timing, power loss on the real partition, CDC throughput, the boot-time entry
gesture, the OLED while maintenance is active) is NOT verified. This is not a release claim.

## Files on the device

LittleFS on the 1 MiB partition, flat root names. The CircuitPython originals live under
`/userdata/Orbit12Orion/` and keep the same leaf names.

| Name | Content | Source |
|---|---|---|
| `Scene01`..`Scene10` | SCN1 scene text | `scenes.py` `scene_file_name` |
| `_activescene` | `N\n`, N = 1..10 | `scenes.py` `ACTIVE_SCENE_FILE` |
| `user_settings` | `key_brightness,<0..1>\n` | `user_settings.py` |
| `<name>.tmp` | staged write, never a committed file | T13 |
| `<name>.bak` | previous version during a commit | T13 |
| `/orbit12.fs` | format stamp (`ORBIT12FS1` plus newline), written last by a verified format | fs_format |

Adapter: `firmware/Orbit12/src/platform/macropad/littlefs_storage.{hpp,cpp}`, the only
T13 file with Arduino or LittleFS includes. Rules:

- `mount()` uses `LittleFSConfig(false)` and then requires the format stamp `/orbit12.fs`. A
  partition that fails to mount, or mounts without the stamp, is reported unmounted and is never
  formatted. Only the maintenance FORMAT frame formats (see "Format" below).
- One file handle at a time (reader or writer), freed on close. The library's per-file
  buffers are allocated by LittleFS on open and freed on close; they are maintenance-only and
  not measured. Mount allocation is library initialization and is also unmeasured.
- Writes, renames and removes are refused unless `set_maintenance_window(true)` was called.
  The application may open the window only when the device is stopped AND core 1 has
  acknowledged quiescence (the architecture's handshake), and must close it before resuming.
  "Transport stopped" alone is not sufficient.
- A short write is reported as Full. Close errors are not visible through the Arduino File
  API; the read-back verification below detects them.

## Commit protocol (scene, pointer, user settings and CDC PUT)

1. Stop: release notes, drain output, quiesce both cores (the existing `SceneService` state machine).
2. Remove any stale `.tmp`, write the new text to `.tmp`.
3. Read `.tmp` back and verify it. For CDC PUT the byte count and CRC of the read-back must
   equal the received payload. Then the content must be valid: SCN1 decode with the schema
   resolver, pointer grammar, or settings parse. Any failure removes `.tmp` and leaves the
   target untouched.
4. Rename target to `.bak` (if it exists), rename `.tmp` to target, remove `.bak`.
5. Only then report success (ACK or `Succeeded`). A `.bak` left by a power cut is restored by
   the next read through `SceneService::recover_file` (target missing and backup present means restore).

LittleFS rename replaces the destination and is a single metadata commit. Power-loss behavior
of the selected core is taken from its documented semantics and is not measured on the board.

## Format (explicit, operator-requested)

Purpose: the 1 MiB partition is formatted as LittleFS only when the operator asks for it. Boot,
normal performance and the CDC pump never erase anything. The format erases every file on the
partition, including leftover CircuitPython data, and cannot be undone. There is no backup.

Procedure (device side):

1. Enter maintenance mode: power up with a pad held down (the first pad sample decides for the
   boot), keep it held for 0.5 s, within the first 5 s after power-up. The engine stops, the OLED
   keeps its last frame and the pads do not respond.
2. From the host, run the format (it refuses without the flag and prints what it erases):

   `python tools/arduino/scene_transfer.py format --port COMx --yes-erase-partition`

   The tool then waits up to 60 s for the device's answer, and on ACK runs `list` to confirm.
3. Start the format within 60 s of entering maintenance: the idle timer restarts with every byte the
   device receives, and 60 s without one ends maintenance. The format itself takes several seconds.
4. Exit maintenance with a plain `Q` byte sent from a serial terminal (the tool does not send it).
   Then power-cycle the device. The boot mount then runs on the new filesystem: confirm
   `STORAGE mounted=1` in the `?` dump, and run `scene_transfer.py list`. Import the scenes as described
   in the import workflow.

Protocol: a frame of type `F` whose name is exactly the 8-byte ASCII token `ERASE-FS` and whose
payload is empty, with a valid CRC-32 (`4F3146080000000045524153452D46533202957C` is the golden
frame). Existing frame types never format: a PUT or GET naming the token is refused as a disallowed
name. The device checks, in order:

- token: name length 8 and exact bytes, else NAK `BadToken` (10) and nothing changes;
- payload: must be empty, else NAK `BadFrame` (1);
- gate: the engine must be stopped (maintenance active), else NAK `Busy` (5). The gate is checked
  again at the commit point;
- storage window: the adapter refuses unless the maintenance window is open. A refusal is a
  `FormatFailed` (11) NAK, and the partition state is left unchanged.

Sequence (`persistence/fs_format.cpp`, host-compiled and tested with a fake LittleFS):

1. Unmount. Erase and create the filesystem (`LittleFS.format()`, which leaves it unmounted).
   Failure: `FormatFailed` (11).
2. Mount without formatting. Failure: `MountFailed` (12); the partition stays unmounted.
3. Probe: write `/fmt_probe.tmp`, read it back byte for byte, remove it, and check it is gone.
   Any mismatch: `VerifyFailed` (13).
4. Stamp: write `/orbit12.fs` (`ORBIT12FS1` plus newline) and read it back. Any mismatch:
   `VerifyFailed` (13).
5. Only now the device ACKs (`A`, echoing the token). The adapter's mounted flag is set to true
   only after this point.

Every format failure (NAK 11, 12 or 13) leaves the partition unmounted, and the dump then reports
`mounted=0`. The other NAKs (`BadToken`, `BadFrame`, `Busy`, `BadChecksum`) are refused before any
erase and leave the filesystem exactly as it was, including its mounted state. A NAK never reports
success, and an ACK is never sent unless step 5 completed.

Why no board nonce: the token, the exact length, the empty payload, the CRC and the maintenance gate
together make an accidental trigger unlikely (stray bytes must match 8 ASCII bytes and a valid CRC
while the engine is stopped). A per-board challenge would need a second command and a stored value
for little gain against a deliberate operator, so it was not added.

Power cut and interruption (defined behavior):

- A cut at any point before the stamp write completes (during the erase, the mount, the probe, or a
  torn stamp write) leaves the next boot unmounted. The boot check requires the stamp, so a
  partially formatted partition is never reported as mounted. Verified on the host for every cut
  point of the sequence (`t13d_format`, power-cut sweep). The next format from that state succeeds.
- Compatibility: a partition that is a valid LittleFS but lacks `/orbit12.fs` now reports unmounted
  at boot. Nothing on this device produces such a partition (CircuitPython uses FAT), so this costs
  nothing here. The alternative was LittleFS mount plus probe only; the stamp was chosen so a
  partition from an interrupted format is never reported as mounted.
- A cut after the stamp write has completed but before the ACK reaches the host leaves a complete,
  verified filesystem that mounts as usable. The format cannot distinguish that case from success
  without a further commit record, so the host should not assume failure there: run `list` (which
  shows the partition) and format again if in doubt. This refines the earlier expectation that every
  cut reports unmounted: the stamp is the last durable write, so no cut can leave a mounted
  partition with a missing or mismatched stamp.
- The LittleFS format and program operations do their own `idleOtherCore()` calls; the core-1
  quiesce is an additional guard.

Diagnostics: the format result is the NAK reason (or the ACK). The `?` dump shows the live
`STORAGE mounted=` value: after each format the value is read from the adapter's own mounted flag,
not inferred from the result. The maintenance session counts
`formats` and `format_failures`. Format is not visible on the OLED (it is frozen in maintenance mode).

What the format does not do: it does not reboot the device, does not re-run the boot scene load
(the running scene stays as it was in RAM until the next power-up), and does not write any scene file.

## Scene load is in place (no reset)

Scene changes never reboot or reset the MCU. The Python app restarted the whole application on
scene change as a memory workaround; the Arduino version does not. A switch runs:

quiesce and release notes -> read and verify target into the staging DTO -> reset fixed
slots -> apply globals, module construction, parameters and attributes in source order ->
commit the active pointer -> rebuild UI -> resume.

If the target file is missing (not corrupt), the switch follows `Orion.load_active_scene`: the stock
defaults are applied in place, saved to the target scene, then the pointer moves to it (the
`ResetDefaults`/`CaptureReset`/`SaveReset` phases with the target, then `CommitPointer`). The SCENE
row shows the committed index after every scene operation, including a refused one.
If target verification fails, nothing live is touched and the current scene keeps running.
If apply fails after the slots changed, the service re-reads and re-applies the previous scene
in place (`RollbackRead`/`RollbackApply`). Only if that rollback also fails does the service
enter `Failed`. Entering `Failed` resumes the core-1 worker (T14 decision, see the record at the
end): the service stays busy but writes nothing, so the worker can scan the recovery gesture and
every pad shows solid red. The gesture is a press of the encoder knob. It calls
`Controller::request_scene_recovery()` -> `SceneService::acknowledge_recovery()`, which requests
a new quiesce (`RecoveryQuiesce`/`RecoveryWait`; the read may rename or remove `.tmp`/`.bak`
files), re-reads the committed active scene and re-applies it in place (`RecoveryRead`/
`RecoveryApply`), then resumes. A failed recovery returns to `Failed` with `RecoveryRequired`;
success reports `Succeeded`. The pointer needs no rewrite because it is only advanced after a
successful apply. All other events are dropped while `Failed`. The `Reset` operation resets to
defaults in place and is not a reboot. Reset rolls back to the committed scene when its save
fails, and the reported status is the storage status, never `ApplyError`.

## Boot (T14)

The LittleFS partition is mounted without formatting (`LittleFSConfig(false)`) and must carry the
format stamp. If the mount fails or the stamp is missing, every scene operation reports an error and
the device runs the stock defaults. The mounted flag in the `?` dump (`STORAGE mounted=`) reports
exactly this.
Boot starts once both cores are ready and runs in `poll()` (nothing waits): `LoadUserSettings`
(KEYBRI), then `LoadActive`. `LoadActive` reads the pointer and the scene but does not rewrite
`_activescene`.

Pointer and default scene (source: `Orion.load_active_scene` keeps the pointer's index): the index
comes from `_activescene` (missing pointer: scene 1). When that scene is missing or corrupt, the
device keeps the index, so a later save targets that same scene and never another one. The stock
state stays in place: the Note input, six Empty processors and the SET/OUT/SETTINGS defaults.
The source would write the default into the missing file at boot. This firmware does not write at
boot. If a load fails after some of
the scene was applied, the stock defaults are re-applied in memory (no storage write), and
`boot_default_fallbacks` counts it. A missing scene at first boot is not counted as a scene
failure (`scene_failures` stays 0). A corrupt scene is counted.

Boot writes nothing except the stale-file cleanup of the recovery protocol above (a `.tmp`
left by a power cut is removed, and a `.bak` is restored when its target is missing). Nothing
is formatted or deleted otherwise. There is no MCU reset or reboot anywhere in the firmware
(searched: `reset`, `reboot`, `watchdog`, `NVIC`, `restart`; only internal names and comments).

## RAM and stack

Measured on the pinned RP2040 compiler (`sizeof`):

| Object | Bytes | Note |
|---|---:|---|
| `SceneDto` (staging) | 10,152 | held inside `SceneService` (10,284 total) |
| `MaintenanceSession` | 260 | no own SceneDto; verifies through the service's staging |
| `UserSettings` | 4 | |

Measured with `arm-none-eabi-g++` 16.1.0 (`sizeof` probe, `-mcpu=cortex-m0plus`). The
`MaintenanceSession` figure was 252 in the previous measurement; this run gives 260 for the
current header and the probe was not re-run against the earlier source.

The CDC verify path borrows the service's staging `SceneDto` (10,152 bytes) and does not own a
second one, so the maintenance path adds no DTO to the budget. The borrow is explicit and guarded:
`MaintenanceSession` is constructed with the `SceneService` and calls `lend_staging()` for each
Scene verify. `lend_staging()` returns nullptr while a scene operation is in progress, and the
verify is then refused with `Busy` (the temp file is discarded and the target is untouched). Every
scene operation refills the staging object before it reads it, so the loan carries no state.
Sharing is safe because maintenance runs only while stopped and idle and the two users never
overlap. Without sharing, the verify path would need a second 10,152-byte object (20,688 bytes
together with the service and session).

Stack (`-fstack-usage` on the real sketch, `build/t13-firmware`): the largest T13 frames are
`write_user_settings_atomic` 320 bytes, the CDC LIST builder 320 bytes, `read_user_settings`
192 bytes, `verify_temp` 184 bytes, and the adapter's worst function 112 bytes. All are well under
the 2 KB per-core stack that the top-risk note records.

Real sketch build (`macropad` profile, includes the renderer work present in the tree at build
time): 222,104 bytes flash (3%) and 58,376 bytes globals (22%). The slice baseline was 191,516 and
57,052; the delta is not attributable to T13 alone and is not separated here.

## CDC maintenance protocol

Implementation: `firmware/Orbit12/src/persistence/maintenance_protocol.{hpp,cpp}`
(host-compilable). Host tool: `tools/arduino/scene_transfer.py`.

Frame: `4F 31 | type | name_len | payload_len u32 LE | name | payload | crc32 u32 LE`, with
CRC-32 as zlib. Requests: `L` list, `G` get, `P` put, `X` abort. Responses: `D` data, `A` ack
(sent only after a verified commit), `N` nak with one reason byte.

Refusals (each a NAK; the target is never changed by a refused frame):

- names: exact allowlist `Scene01..Scene10`, `_activescene`, `user_settings`. Path separators,
  other case, `Scene00`/`Scene11`, and `.tmp`/`.bak` names are refused;
- payload bounds: Scene <= 26,553 bytes, pointer <= 4, user settings <= 128. An oversize payload
  is discarded without buffering and framing resynchronizes;
- checksum mismatch; byte-level read-back mismatch (storage kept fewer or different bytes);
  content invalid after decode;
- not stopped, or a scene operation holds the staging object (`Busy`): checked at dispatch, at every 64-byte flush, and before commit, so a gate
  that closes mid-upload discards the transfer;
- storage full or storage error: temp file removed, target kept.

A frame with an unknown type or a name length above 16 is a framing error: the device replies `N`
and resynchronizes on the next sync bytes, so the host must restart the request.
`MaintenanceSession::abort()` (called by the application on disconnect or timeout) removes the
`.tmp` file and never touches the target.

## CIRCUITPY export and import workflow (before flashing Arduino firmware)

1. Export from the running CircuitPython device. Copy from the CIRCUITPY drive into a host folder,
   keeping the leaf names: `userdata/Orbit12Orion/scenes/Scene01..Scene10`,
   `userdata/Orbit12Orion/scenes/_activescene`, and `userdata/Orbit12Orion/user_settings`.
   Keep this folder: it is the only backup of the scenes until the import is verified.
2. Flash the Arduino firmware through the bootloader UF2 (the normal firmware path). The firmware
   never formats by itself. A partition left as CIRCUITPY (FAT) or otherwise unformatted mounts as
   unmounted; format it explicitly (see "Format" below) before importing.
3. Enter maintenance: hold any pad at power-up for about 0.5 s (the first 5 s after power-up).
   The device stops; the OLED keeps its last frame and the pads do not respond. Then import:
   `python tools/arduino/scene_transfer.py import --port COMx --from <folder>`
   (`--dry-run` first; `--only Scene01 user_settings` limits the set). Each file is reported ACK or
   NAK. A NAK stops the import; files already acknowledged remain valid. The tool does not send the
   exit byte: finish with `Q` from a serial terminal (outside a frame), or wait 60 s with no byte.
   Keys are not performed until the exit resync completes (release every pad once).
4. Verify with `python tools/arduino/scene_transfer.py list --port COMx`, then export back to a
   new folder and compare.
5. Imported scenes take effect when selected. An import never replaces the running scene.

Export later from the Arduino device with `export --port COMx --out <folder>`. The CircuitPython
firmware is the rollback path: copy the exported folder back to its `userdata/Orbit12Orion/`
location.

The maintenance entry and the device-side CDC pump are described in "Maintenance mode (T14)"
below. The protocol and host tool are tested on the host only.

## User settings (KEYBRI)

`UserSettings::key_brightness` (default 1.0, matching `NeoPixels.brightness_multiplier`) is parsed
as Python float text: optional sign, digits with an optional fraction, optional exponent. Values
are rounded to micro-units and must be in 0..1 after rounding. Unrounded binary noise
(`0.30000000000000004`), exponents (`2.7755575615628914e-17`) and `-0.0` are accepted. Out-of-range
values (`1.5`, `-0.1`, `1e5`), non-finite text, hex, and malformed lines are rejected, never
clamped, and the current value is kept. Unknown names are ignored. Writes use at most six fractional
digits.

`SceneService` owns `SaveUserSettings` and `LoadUserSettings`. A missing file yields the default with
`user_settings_present() == false`; a load never creates the file.

Controller integration (T14): boot copies the loaded value into the brightness multiplier and
refreshes the KEYBRI row. Encoder edits only set a dirty flag. Leaving the KEYBRI edit requests
`SaveUserSettings` once the transport is stopped and the service is idle. A running transport
keeps the save pending, so a brightness change never stops a performance. Python saved on every
encoder tick. The save is not per tick here.

Deviation from the Python loader: a value with a leading `-` that is not zero is rejected, not
clamped to 0, and a line without a comma is rejected rather than ignored. This follows the
"invalid persistent values are rejected" rule.

## Decision record: SceneService `Failed` phase (T14 update)

Status: implemented in T14, pending lead acknowledgement. It departs from the Option B text ("stays
quiesced"). Entering `Failed` resumes the core-1 worker, because a quiesced worker cannot scan the
recovery gesture. Recovery requests a new quiesce before it reads. Failed entries are counted in
`scene_failures`, one per entry: a failed rollback and each failed recovery. Successful recovery is
not counted. Ordinary failures that return to idle are counted at idle, as before.

Also a T14 behaviour change beyond item (g): a `Reset` now rolls back to the committed scene when
its save fails. A rollback that fails enters `Failed` (a new path into `Failed`).

## Maintenance mode (T14)

Entry (`EngineApp::service_gesture`): mirrors `boot.py`, which selects USB mode when any key is held
at power-up. The first core-1 pad sample decides for the boot: if no pad is down, the gesture is
disabled until the next power-up (keys held at power-up produce no edges, only a mask). When armed,
a pad held for 0.5 s during the first 5 s of uptime requests maintenance, and the request is retried
until no scene operation runs. Consequence: a pad held continuously from power-up still enters
maintenance, as in the source, and a pad pressed after boot never does.

Sequence: the controller stops (`enter_maintenance`: notes released, transport stopped) -> core 0
drains the release packets -> core 1 is quiesced through `MaintenanceHandshake` (core 1 touches no
peripheral until resumed; OLED and pads freeze) -> the storage window opens -> the CDC pump serves
`MaintenanceSession` frames. If core 1 does not acknowledge within 1 s, the request is cancelled and
performance continues.

While active the engine is silent: incoming MIDI is discarded, musical input is not processed, and
the output queue only drains. A response write waits at most 2 s for the host to read. After that
the write fails, and every later write fails at once until the host sends a byte. A GET therefore
costs at most one wait, not one per 64-byte chunk.

Exit: a plain `Q` byte received between frames, or 60 s with no byte received. Exit aborts an open
transfer (its `.tmp` is removed, the target is untouched), closes the window, resumes core 1 and
marks the physical epoch changed. Physical keys must be released and resynced before musical input
is accepted again. No reboot is involved.

Limitations: the OLED shows its last frame while maintenance is active (core 1 is quiesced by
design), and the entry gesture is not visible on the device. Both are accepted for T14 and need a
hardware check. `LittleFS` program and erase also call `rp2040.idleOtherCore()` themselves, so the
handshake is an additional guard and not the only one.

## Verified and not verified

T14 (host, Release, CTest 35/35 in `build/native-t14`; `t14_slice` is the integration rig with
a fake LittleFS and a guarded write window):

- Boot without a scene: stock defaults, no file created, no write, window closed, first boot not
  counted as a failure. Boot with a scene (and KEYBRI 0.5): loaded in place, no write. Corrupt
  scene: defaults applied, counted, file not overwritten.
- In-place switch to scene 2: pointer `2\n`, window opened and closed, resync after the transition,
  input works after it. No write outside the window in any test.
- Failed switch (power cut at pointer commit): `Failed`, pads red, events dropped, one failure
  counted; the encoder gesture after power returns restores the committed scene, no new failure.
  A failed recovery is counted again.
- Maintenance: a held pad sounds a note; entry releases it; no MIDI while active; LIST served; a
  PUT of `Scene11` NAKed with no write; `Q` exits; the same pad sounds again after the resync.
  Idle timeout exits. `module_at` and the selector match the source registry order.
- `chord_walk` scene: its constructor's key offset (-2) survives the scene load as in source.
- `t07b`: Reset save failure reports `StorageError` and re-applies the committed scene; LoadActive
  performs no write; Failed/recovery resume and failure counts. `t11b`: leaving KEYBRI saves.

T14 format (host, Release, `build/native-fmt`, CTest 37/37 including `t13d_format` and `t14_slice`):

- `t13d_format`: the format sequence against a fake LittleFS: success erases old data, probes,
  stamps, leaves the partition mounted and usable; format, mount and verify failures each leave it
  unmounted; a silently dropped write is caught by read-back; an unstamped or CIRCUITPY-like
  partition does not mount; a power cut after each mutation (torn writes included) leaves the next
  boot unmounted, and a retry succeeds. Protocol: wrong, lowercase, missing and over-long tokens are
  refused with `BadToken` and never format; a correct token with a payload, a bad CRC, or while not
  stopped is refused; a NAK reason for each failure; no existing frame (LIST, GET, PUT, ABORT, the
  token used as a name) formats.
- Target: `persistence/fs_format.cpp`, `maintenance_protocol.cpp`, the LittleFS adapter and
  `app_main.cpp` compile with the pinned flags (`-Wall -Wextra`, no warnings). Hardware is not run.
- `t14_slice` `unmounted_boot_maintenance_format_recovers`: boot with an unmounted partition and
  every port operation failing reaches Idle, not Failed, and not busy; the pad gesture enters
  maintenance; LIST and PUT are refused honestly; a wrong token changes nothing; the correct token
  ACKs and the dump flag reads mounted; LIST and a `user_settings` PUT then succeed in the same
  session; `Q` exits. The format callback in this rig is a stand-in for the verified sequence,
  which `t13d_format` covers.
- Host tool: `scene_transfer.py selftest` (includes the golden FORMAT frame) and the refusal without
  `--yes-erase-partition`. The ACK-then-list flow was exercised with a stubbed link only.

Target (pinned arm-none-eabi-g++ 16.1.0): `tools/arduino/test-target-domain.ps1` passes
(`domain/` and `persistence/`, `-Werror -Wstack-usage=8192`). The app sources compile with the same
flags except `app/app_main.cpp`: GCC reports `-Wstringop-overflow` on `SpscQueue::try_push`
(`bridge.hpp:46`; likely a false positive in the existing bridge code, unconfirmed; owned by Sol).
The same `try_push` calls were in the headless slice. Full sketch with the
`macropad` profile: 233,900 B flash (3%), 59,440 B globals (22%; the T13 build was 58,376 B); arduino-cli
reports no warnings. Application-owned new public surface: `ControllerPorts::window` and
`StorageWindowPort`, `AppStorage` and the new `EngineApp` constructor, `SharedLink::maintenance`,
the non-const `Controller::controller()`, the boot and maintenance methods of `Controller`,
`SceneService::failure_count()`, the phases `RecoveryQuiesce`/`RecoveryWait`, the new
`ControllerDiagnostics` fields, and `kDumpCapacity` 4096 (one copy, `EngineApp::dump_` in `g_engine`; +1024 B static RAM over the earlier 3072 cap, which had been +512 B over 2560). Worst-case dumps with 10-digit counters are about 3.1 KB, so the 4096 cap keeps long-uptime dumps whole; a cut still reports `TRUNCATED dumps=N` in the reserved 64 B tail.

Stack (`-fstack-usage`, profile optimization, static sums along core-0 call chains, excluding
library and interrupt frames): playback about 1.2 KB (`EngineApp::poll` 104, `Controller::poll` 88,
`step` 64, `process_pass` 312, `Chain::process` 392, the largest processor 264); scene save/switch
encode about 1.2 KB (`write_scene_atomic` 80, `encode_scene` 136, float text `constprop` 344,
`round_decimal` 304); scene decode (switch, boot, CDC PUT) about 1.1 KB (`decode_scene` 432,
`scene_parse_float` 160, `scene_resolve` 112); CDC LIST about 1.0 KB including LittleFS frames.
Core 1 `UiWorker::poll` 200 and `render_screen` 208. Static sums are not measurements: the 2 KB
per-core stack also holds TinyUSB and interrupt frames, which are unmeasured. The `STACK0` line of the
`?` dump is the measurement path, and it has not run on hardware.

Host-verified (CTest, `build/native-w2`, Release, 33 of 33 targets pass; the t14_slice link failure
noted earlier no longer occurs; superseded by the T14 results above):

- `t07b_scene_service`: forced rollback failure (target and rollback apply both fail) leaves the
  service `Failed` and quiesced with no resume; the re-apply fails again and keeps `Failed`; a
  cleared fault recovers in place to the committed scene (not defaults) and resumes. A recovery read
  failure keeps `Failed` until the committed file is readable.
- `t13a_protocol`: a Scene PUT whose verify runs while a scene operation holds the service is refused
  with `Busy` and changes nothing; the same PUT on an idle service commits through the shared
  staging object.
- `t11b_scenes`: `octave_offset_round_trip_with_other_slot_active`: octave and key_offset changed by
  note navigation while a non-IN chain is active are mirrored into the IN rows and survive a save and
  load round trip (intentional deviation from the Python conditional mirror; see known-quirks.md).
  The check is not mutation-tested against the old conditional mirror.
- `t13a_protocol`: golden frames generated by Python zlib; checksum; truncation then abort;
  oversize discard and resynchronization; disallowed names; busy gate (including a gate that closes
  mid-payload); storage full; silent short write; invalid content; unknown frame type; GET and LIST.
- `t13b_user_settings`: parse and encode rules, Python float forms, save and load round trip, missing
  file, invalid file, failed save leaves the previous file, and a power-cut sweep over the save.
- `t13c_interrupted`: a power cut after each mutating operation of a CDC PUT to `user_settings` and
  to `Scene01`, followed by boot recovery through `SceneService`. Result: old or new content with
  status Succeeded, never corrupt, and an acknowledged PUT always stores the new content.

Target-compiled:

- `persistence/*` and `domain/*` with `tools/arduino/test-target-domain.ps1` (pinned arm-none-eabi-g++
  16.1.0, `-Werror -Wstack-usage=8192`): passed after the Option B and shared-staging changes.
- The LittleFS adapter with arduino-cli for the pinned FQBN, `-Wall -Wextra`: no diagnostics from T13 files.
- The full `firmware/Orbit12` sketch with the `macropad` profile: compiles.

Not verified: LittleFS power-loss behavior on the partition, flash timing of commits and of the
format (the erase of the 1 MiB partition takes an unmeasured time), CDC throughput and jitter during
maintenance, the maintenance entry gesture, the device-side serial pump (T14), the format on the real
partition (the first run is hardware-only), and scene switch timing on hardware.
