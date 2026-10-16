# Storage, scene lifecycle and CDC maintenance (T13)

Status: host-verified (t07b, t11b, t13*, t14_slice) and target-compiled. Hardware behavior
(flash timing, power loss on the real partition, CDC throughput, the boot-time entry
gesture, the OLED while maintenance is active) is NOT verified. This is not a release claim.

## Files on the device

LittleFS on the 1 MiB partition, flat root names. The CircuitPython originals live under
`/userdata/Orbit12Orion/` and keep the same leaf names.

| Name | Content | Source |
|---|---|---|
| `Scene01`..`Scene16` | SCN1 scene text, one file per slot (slot index 0..15) | `scenes.py` `scene_file_name` (10 slots there; 16 here) |
| `scene_names` | `NN,NAME\n` per occupied slot (NN = 01..16, NAME = 1..8 chars of A-Z 0-9) | new (named scenes) |
| `_activescene` | `N\n` or `NN\n`, N = 1..16 (1-based slot number) | `scenes.py` `ACTIVE_SCENE_FILE` |
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
4. Exit maintenance with a plain `Q` byte sent from a serial terminal (the tool does not send it), or
   with an `EXIT-MAINT` frame (`scene_transfer.py exit --port COMx`; see "Host-initiated maintenance").
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
(KEYBRI), then `LoadNames` (named slots, see below), then `LoadActive`. `LoadActive` reads the
pointer and the scene but does not rewrite `_activescene`. A failed `LoadNames` does not stop
`LoadActive`.

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
is formatted or deleted otherwise. One exception, first run only: when the names file is absent
(or present but empty), boot creates `ALFA` in slot 0 from the stock defaults (`CreateScene`
without a previous scene: scene file, then names file, then pointer). Boot after that finds the
names and writes nothing. A names file that is unreadable, oversize, or holds no valid line is
never replaced at boot: `LoadNames` reports the error and the slot files stay untouched.

## Named scene slots (phase 1: persistence)

Sixteen slots, `Scene01`..`Scene16`. A slot is occupied exactly when `scene_names` has a line for
it. Names: 1..8 characters from `A-Z` and `0-9`, unique across slots. Files for unoccupied slots
are ignored: a stale numbered `SceneNN` with no names entry is never loaded, and a new scene
created in that slot overwrites it ("start clean").

- `scene_names` text: one `NN,NAME\n` line per occupied slot, ascending slot order, CRLF accepted
  when parsing. Bound: 256 bytes (`kSceneNamesMaxBytes`). Parsing skips malformed lines (bad shape,
  bad name, out-of-range or repeated slot, repeated name) and counts them in
  `SceneNames::malformed_lines`; it never fails on content. The device's CDC PUT of `scene_names`
  is strict instead: any malformed line is refused (`InvalidContent`).
- Persistence API (`persistence/scene_names.hpp`, host-compilable, no heap):
  `SceneNames` (16 x 9-byte names, `occupied_mask`, `malformed_lines`), `name_valid`,
  `name_taken`, `first_free_slot`, `first_occupied_slot`, `suggest_name` (the next unused NATO
  word: ALFA, BRAVO, ... ZULU; a bounded `<word>NN` suffix only if all 26 are taken),
  `parse_scene_names`, `encode_scene_names`, `parse_active_pointer`, `format_active_pointer`.
- `SceneService` (`persistence/scene_service.hpp`):
  - `request(SceneOperation::LoadNames)`: boot names load. Creates ALFA on first run.
  - `request_create(const char* name)`: validates, then picks the first free slot. Refusals set
    `status()` and return false, with no operation started: `NameInvalid`, `NameTaken`,
    `NoFreeSlot`. Accepted, it runs `CaptureCurrent` and `SaveCurrent` (only when the active slot is
    named), `ResetDefaults`, `CaptureReset`, `SaveReset` (the new scene file), `WriteNames`,
    `CommitPointer`, `Rebuild`. A failed pointer commit restores the names file to the set without
    the new name (`RestoreNames`). Rollback to the active scene happens only when a named active
    scene exists.
  - `request(Switch|Load|CopyCurrent, slot)` refuses an unnamed slot with `SlotUnoccupied`. The
    active slot therefore always has a name once names are loaded.
  - `request(Save)` is unchanged.
  - `LoadActive`: a pointer at an unnamed slot falls back to the first named slot (only once names
    are loaded).
  - Read-only accessors for the UI: `slot_occupied(slot)`, `slot_name(slot)` (empty string when
    unoccupied), `active_slot()`, `occupied_count()`, `scene_names()`.
- Commit order for a create: scene file, names file, pointer. A power cut between them leaves the
  old state, or a named but not yet active scene (the new name is always backed by its scene file).
  Memory changes only after the names file verifies.
- Trust gate: `names_loaded()` is true only after a `LoadNames` succeeded (including the first-run
  ALFA creation) and nothing since has made the in-memory set stale. It is false at construction,
  after a failed `LoadNames` (unreadable, oversize, or no valid line), after a `RecoveryRequired`
  names write, after a failed names restore, and after `invalidate_names()`. While it is false,
  `request_create` and `request(Switch|Load|CopyCurrent)` report `NamesUnavailable` and start
  nothing. This is what stops a create from overwriting an unread names file or a real Scene01.
  `Save` and `LoadActive` are unaffected.
- CDC: `scene_names` is allowlisted with the scene files (`list`, `get`, `put`, `export`, `import`
  in `scene_transfer.py`). A committed `scene_names` PUT and a successful FORMAT call
  `invalidate_names()`: the names on storage changed, so create and slot operations are refused
  until the next boot. A refused upload changes nothing. The pointer PUT still applies at the next
  boot, as before.
- Dump: the `STORAGE` line carries `names=<occupied count> names_ok=<names_loaded> names_bad=<lines
  skipped by the last parse>`.
- Cost: `SceneNames` 152 bytes and the pending name 9 bytes inside each `SceneService`; the names
  write path holds one 256-byte encode buffer and the verify path a second 256-byte buffer on the
  stack (`write_names_atomic`, `write_text_atomic`), within the `-Wstack-usage=8192` gate. Measured
  on the pinned build: flash 237,276 -> 240,220 bytes, global RAM 61,276 -> 61,460 bytes. Hardware
  stack headroom is not measured.
- Controller (phase 2, see "Name editor (phase 2)" below): the SCENE row lists the named slots in
  slot order plus [NEW]. Creation is the [NEW] editor; rename and delete are not implemented.
  There is no MCU reset or reboot anywhere in the firmware
(searched: `reset`, `reboot`, `watchdog`, `NVIC`, `restart`; only internal names and comments).

## Name editor (phase 2)

- SCENE row (STTNGS, row 0): a `RowKind::SceneSelect` list of the occupied slots in slot order, then
  `[NEW]` (list length = occupied + 1). The knob moves through the list (wraps both ways); releasing
  on a name switches to that slot with the existing Switch path; releasing on `[NEW]` opens the
  name editor. The row value text is the entry's scene name, cut to the first five characters the
  value field holds (the highlighted row is scale 2, the others scale 1; right aligned). `[NEW]` shows
  `NEW`. While the row is being edited, the value follows the hovered entry.
- Footer text rules (first match wins; the footer field is 17 cells, names are at most 8):
  1. Name editor: a message (`NAME INVALID`, `NAME TAKEN`, ...) until the ring next moves; else the
     meaning of the selected control (`CONFIRM`, `CANCEL`, `DELETE`); else the length (`LEN 5/8`).
  2. SCENE row highlighted (parameter selection or edit): in selection, the active scene's full name
     (same as the default); in edit, the hovered entry's full name, or `NEW`.
  3. Pattern row highlighted: the X (hit) / O (rest) steps of its current value in selection, of the
     hovered value in edit. Steps are MSB first from the shared `music::kPatternBits` and
     `kPatternLengths` tables (at most 8). The arp and arp_walk pattern rows apply RYSHFT when playing;
     the preview shows the unshifted pattern. wander's RTHM row uses the same tables. Gate, chopper,
     retrigger, drop and strum have no pattern row, so they get no preview.
  4. Default: the active scene's name (uppercase, up to 8 characters). With no names read (boot
     names load failed or not yet done), the footer falls back to `SCN NN`.
  Module selection (UiSection::ModuleSelection), after the name-editor message rule: the footer shows
  the highlighted module's description from `app/module_descriptors.hpp`, looked up by registry id
  (never by label: CHRD is both the chord processor and the chords input). IN lists inputs; other
  slots list processors, Empty included. Leaving the list restores the default.
  Leaving the parameter (moving the highlight to another row) restores the default. The footer text is
  copied into the snapshot and bounded to 17 characters.
- Name editor screen (renderer `draw_name_editor`): the typed characters are drawn in the small font
  (scale 1, pitch 8 px, x from 26, y 29) in one row. The current ring item is drawn in the big font
  (scale 2, white on black, not inverted) in the cell right after the typed characters (index =
  length; after the eighth letter when the name is full), at y 26. Up and down arrows (7 px wide,
  apex at y=19, base at y=40) sit above and below the big cell. The widest case (eight small letters
  plus the big cell) spans x 26..101; the group is centred. Controls read as `<` (delete), `#`
  (confirm), `*` (cancel); all three exist in `kFontChars`.
- Name editor (`UiSection::NameEdit`): the name (up to 8 characters, `A-Z` and `0-9`) is typed with a
  ring that wraps both ways: `A`-`Z`, `0`-`9`, then DELETE (`<`), CONFIRM (`#`, the initial ring
  position), CANCEL (`*`). The footer words are DELETE / CONFIRM / CANCEL. When the name is full
  (8 characters) the ring holds only DELETE, CONFIRM, CANCEL, in that order, and wraps among them; a
  letter that makes the name full puts the ring on CONFIRM. The editor opens with the `suggest_name`
  word prefilled (`ALFA` on a fresh device), so one CONFIRM press creates it. With no free slot there is no
  prefill and the confirm reports `NO FREE SLOT`.
  - Encoder press on a ring item accepts it: a letter or digit appends (the cursor moves on), DELETE
    removes the last character (no-op when empty), CONFIRM calls
    `request_create(name)`. A refusal (`NAME INVALID`, `NAME TAKEN`, `NO FREE SLOT`, `NAMES NOT LOADED`)
    stays in the editor with the message in the footer. Acceptance leaves the editor for the SCENE row;
    the create outcome arrives later through `poll()` and the rebuild returns the UI to the chain, as for
    a switch. A storage failure reports the usual scene failure and commits no name.
  - CANCEL returns to the SCENE row (parameter selection) with nothing changed. The editor is not
    reachable while a scene operation is busy: those events are dropped (`events_busy`).
  - In the editor, key-downs start no note and do not start the clock. Key-ups are always forwarded, so
    a note held on entry is released. Stop (encoder held + KEY1) still stops the clock. Other shortcuts
    are not used there.
- Untrusted names: `SAVSCN` (save to the active slot) is refused while `names_loaded()` is false. It
  writes nothing, sets the last status to `NamesUnavailable`, and counts a scene failure. Malformed
  names lines are skipped as before. Names reload only at the next boot.
- Measured on the pinned build (`build-firmware.sh`, release, 0 warnings): flash 242,588 bytes, global
  RAM 61,628 bytes (phase 1: 240,220 and 61,460). `sizeof(UiModel)` is 300 bytes on the RP2040 compiler
  (limit 640). Hardware behaviour of the editor and the footer is not verified.
- Not implemented: rename and delete. CPYSCN still lists only slots 01..10 (its copy target is
  refused when unnamed), so copying into slots 11..16 needs the editor-style list (a later phase).

## Input banks (per-scene input edits)

Each scene keeps the parameter values and saved attributes of the input modules that are not
active. The active input stays in its `M` record. Switching the IN slot's module parks the module
being left (its parameters and saved attributes) and restores the bank of the module entered, if
it has one; a module never edited has no bank and starts from defaults, as before.

- Scope: the six input modules (`note`, `chords`, `drum`, `matrix`, `note_walk`, `chord_walk`).
  Processors keep no bank. Runtime state (held notes, walk positions) is not kept: the switch still
  releases notes as today. The global values mirrored into IN rows (`octave`, `key_offset`) are shared
  and are skipped.
- Touched: the active input is marked when a parameter is edited or a key is pressed while it is
  active (matrix pads edit its attributes). A bank is written when a module is left while touched,
  or when a scene carrying its bank is loaded. Scene saves write the banks of the non-active inputs
  only.
- SCN1 text: after the modules and before `END`, one section per bank:
  `BANK,<id>` then `BP,<parameter>,<value>` lines and `BA,<attribute>,<v>|<v>...` lines, in the
  existing value conventions. Old firmware ignores the `BANK`, `BP` and `BA` tags as unknown lines
  (the active module's `P`/`A` lines are not affected). A file without bank lines loads with all
  banks untouched.
- Parser policy: an unknown bank id is skipped with its lines. A bank id repeated, a value out of range
  or a malformed line rejects the scene (`Malformed`, `InvalidValue`, as for module lines), and nothing
  is applied. A bank parameter name that does not exist is tolerated.
- Layout (`kSceneInputLayout`, `scene_schema.hpp`): each input has a fixed region of persisted
  parameter slots (the schema's persisted parameters, including the global `octave` and `key_offset`
  slots: note 6, chords 13, drum 6, matrix 17, note_walk 5, chord_walk 11) followed by its attribute
  elements (matrix 8 x 12, chord_walk 4 x 4). The regions total 170 scalars.
- Bound: the worst case is all six banks: 6 `BANK` + 58 `BP` + 12 `BA` lines. `kSceneBaseRecordLines`
  159 + 76 + 8 headroom = 243 lines x 159 bytes = `kSceneMaxBytes` 38,637 (was 26,553). The payload
  limits of the maintenance protocol and `scene_transfer.py` follow it. The worst case measured:
  1,252 bytes for an all-banks scene with zero values; real scenes are far smaller.
- RAM: `SceneDto` +732 bytes (10,884 bytes now: the bank staging, decoded and then copied), and the
  controller's parked banks +732 bytes. Measured with the RP2040 compiler: `SceneInputBanks` 732 bytes.
  Global RAM 61,868 -> 63,340 bytes; flash 243,460 -> 246,492 bytes (both measured on the release build).
- Flash partition (1 MiB, LittleFS): 16 scene files at the worst case are 16 x 38,637 = 618,192 bytes.
  A create or rename writes a temp copy and a backup of one scene, so the peak is about 695 KB,
  leaving more than 300 KB for the names, the pointer, user settings and the file system metadata.

## Rename the active scene (phase 3)

- STTNGS rows: SCENE (0), SAVSCN (1), RNMSCN (2, a button), CPYSCN (3), RSTSCN (4), KEYBRI (5).
- Gesture: on the STTNGS page, turn to RNMSCN and press-release it. The name editor opens prefilled with
  the active scene's name, the cursor after its last letter and the ring on CONFIRM. Edit with the
  usual ring (letters and digits append, DELETE removes the last one, CONFIRM applies, CANCEL leaves).
- CONFIRM renames the active scene. The name must be 1 to 8 characters of `A-Z`/`0-9` and must not be
  held by another saved scene. The same name is a no-op (nothing is written). A refusal stays in the
  editor with the message (NAME INVALID, NAME TAKEN, NAMES NOT LOADED). CANCEL returns to the RNMSCN
  row and changes nothing.
- SceneService `request_rename(name)` (operation `RenameScene`) follows the create discipline: the
  quiesce handshake, then the names file written through the same temp, verify and rename path as
  create. Memory takes the new name only after the names file verifies. The scene file and the pointer
  are not touched, and the UI is not rebuilt (the editor's row keeps the cursor). A failure reports
  `StorageError` and keeps the old name in memory; the names file is unchanged.
- Refusals: `NameInvalid` (bad characters or length), `NameTaken` (another slot), `NamesUnavailable`
  (names not read at boot), `SlotUnoccupied` (no active named slot). Each returns false with the status
  and does not start an operation.
- After a successful rename the footer default and the SCENE row show the new name (the names are
  committed before the settings rows are rebuilt).
- Power cut (t13c `rename_scene_power_cut_sweep`): at every cut the names file parses and holds the old
  or the new name, the scene file is byte-identical, and the pointer is unchanged.

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

- names: exact allowlist `Scene01..Scene16`, `_activescene`, `user_settings`, `scene_names`. Path
  separators, other case, `Scene00`/`Scene17`, and `.tmp`/`.bak` names are refused;
- payload bounds: Scene <= 38,637 bytes, pointer <= 4, user settings <= 128, scene names <= 256. An oversize payload
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

## Host-initiated maintenance (INFO, ENTER, EXIT)

These frames let a host enter and leave maintenance without the boot pad gesture. The pad gesture
stays as it was. Implementation: `persistence/maintenance_command.{hpp,cpp}` (normal-run framer and
the reply encoder), `persistence/maintenance_protocol.{hpp,cpp}` (maintenance session; INFO and EXIT
handling), `app/maintenance_info.{hpp,cpp}` (INFO text), `app/app_main.cpp` (the gate, the reply
outbox and the state changes). Host tool: `scene_transfer.py info | enter | exit`.

Frame layout is the same as the rest of the protocol: `4F 31 | type | name_len | payload_len u32 LE |
name | payload | crc32 u32 LE`, CRC-32 (zlib) over every byte before it.

| Frame | type | name (exact) | payload | Reply |
|---|---|---|---|---|
| INFO | `I` | empty | empty | `D` text (normal run and maintenance) |
| ENTER | `M` | `ENTER-MAINT` (11 bytes) | empty | `A` (name echo) once maintenance is Active; else `N` |
| EXIT | `E` | `EXIT-MAINT` (10 bytes) | empty | `A` (sent before the device leaves maintenance); in normal run `N` 14 |

Golden frames (zlib, as hex):

- INFO: `4F314900000000006AFADCB3`
- ENTER: `4F314D0B00000000454E5445522D4D41494E54A95EA0C5`
- EXIT: `4F31450A00000000455849542D4D41494E54F2E28AB3`
- ACK for ENTER: `4F31410B00000000454E5445522D4D41494E54F836B850`
- NAK 16 (NotStopped) for ENTER: `4F314E0B01000000454E5445522D4D41494E541008089424`

The INFO reply is a `D` frame with an empty name echo and an ASCII payload, one `key=value` group per
line (LF terminated, at most 512 bytes; the build asserts that the stamped identity fits):

```
INFO fw=<firmware version> sha=<git sha> api=<engine api> board=<board id>
MODULES count=<n> core=<id,id,...> user=
STORAGE mounted=<0|1> scenes=<named slots in memory> names_ok=<0|1>
STATE maint=<0 normal|1 maintenance> transport=<0|1>
```

`core` lists the registry ids in registry order (24 today; `user` is empty, there are no user modules yet).
`scenes` is the number of named slots held in memory and is 0 while the names are not trusted (`names_ok=0`).
The text comes from `build_info.hpp` (`fw`, `sha`, `api`, `board`) and is never truncated: a text that does
not fit is not sent.

NAK reasons added for these frames (the existing reasons are unchanged):

| Code | Name | When |
|---|---|---|
| 1 | BadFrame | INFO or ENTER carries a payload, or INFO carries a name; an unknown type or a name longer than 16 |
| 2 | BadChecksum | CRC mismatch (nothing changes) |
| 5 | Busy | ENTER: boot load or scene operation in flight, scene service Failed, name editor open, or already held |
| 10 | BadToken | ENTER or EXIT name is not its token exactly (nothing changes) |
| 14 | NotInMaintenance | EXIT while in normal run, or any data request (`L`, `G`, `P`, `X`, `F`) in normal run |
| 15 | AlreadyInMaintenance | ENTER while maintenance is active |
| 16 | NotStopped | ENTER while the transport runs, a note is held or owed, or output is pending |
| 17 | QuiesceTimeout | ENTER: core 1 did not quiesce within 1 s; the request was cancelled and the device keeps performing |
| 18 | ReplyOverflow | INFO: the reply could not be composed or queued (the build-time assert keeps this unreachable for the stamped identity) |

ENTER gate (checked in normal run, before anything changes): transport stopped, no held or owed note,
output nothing pending, boot complete, scene service idle and not Failed, no name editor open, not already
in maintenance. A refusal leaves the device exactly as it was. An accepted ENTER runs the same sequence as
the boot gesture: the controller stops (notes released), core 1 is quiesced through the existing handshake,
the storage window opens, and only then the `A` is sent. A host must not send data requests before that ACK.

Known limit: a MIDI unplug with a note sounding leaves that note held (its sounding bit survives the
disconnect) until the MIDI host is connected again, so ENTER answers `N 16` in that state. The pad
gesture is not affected by this check (it has always stopped and released first). Hardware not checked.

Normal-run framing (`RunCommandFramer`): only complete frames with the sync bytes are consumed. Every other
byte is passed to the `?` dump, unchanged. Commands are `I`, `M` and `E`. A data request (`L`, `G`, `P`, `X`,
`F`) is refused with 14 at its header and its payload is discarded (at most 38,701 bytes; a longer declared
payload is scanned, not discarded). A partial frame that stops arriving for 250 ms is abandoned and its
bytes after the sync are scanned again, so a `?` that followed it still dumps. The framer holds at most
the 28 bytes of one frame. A discard that stops arriving is dropped without replay (payload is data).

Replies in normal run go through a 592-byte outbox (one INFO frame and two NAKs) written without waiting.
A stream owns the port until it ends: a dump that has started finishes before any queued reply, and a
new dump is built only when the outbox is empty, so a reply never lands inside dump text. Entering
maintenance abandons a dump in progress and drops its queued replies. A host that does not read
loses its reply (`reply_dropped=` in the `?` dump counts it). Maintenance replies are written with the
existing bounded wait.

Maintenance mode: the session accepts INFO and EXIT as well as its data requests. ENTER there is refused
with 15. EXIT sends the ACK and then ends maintenance in place (window closed, core 1 resumed, an open
transfer discarded, the physical epoch resynced), exactly as `Q` and the 60 s idle exit do. No reset.

Exit paths, all the same: an `EXIT-MAINT` frame, a plain `Q` byte between frames, or 60 s without a byte.

Dump: the `?` dump has a `RUNCMD` line (`commands`, `refused`, `bad_frames`, `bad_crc`, `bad_token`,
`gap_resync`, `discard_drop`, `info`, `enter`, `enter_refused`, `reply_dropped`). At worst it is 237 bytes
(eleven 10-digit counters). The dump was about 3.1 KB worst case before it, so about 3.3 KB now, still
under the 4032-byte body limit (`kDumpCapacity` 4096 less the 64-byte tail).

### For the installer

Contract for the tool that reflashes the device (`tools/installer/**` belongs to another worker, who builds
on this section). Steps in order:

1. INFO. Check `board=adafruit_macropad_rp2040` and `api=1`. `STATE maint=1` means the device is already in
   maintenance (a pad gesture or an earlier session): go to step 3.
2. If `transport=1`, the operator stops playback first (encoder held and the first pad). Then ENTER. `N 16`:
   playback runs, a note is held, or output is pending; stop and retry. `N 5`: a scene operation, the name
   editor, or the Failed state; wait and retry. `N 15`: already in maintenance; go to step 3. ACK: active.
3. Idle timeout: 60 s without a received byte ends maintenance. Every byte resets the timer, and opening the
   port sends nothing. Start the backup within 60 s of the ACK, or send INFO as a keepalive at least every
   60 s during long steps. A data request answered with `N 14` means maintenance timed out: go back to step 2.
4. Back up: `scene_transfer.py export --port COMx --out <folder>`. Keep the folder until the restore is verified.
5. Reflash. The firmware has no reboot-to-bootloader frame. How the host reaches the UF2 bootloader (BOOTSEL,
   or the 1200-baud touch on this TinyUSB build) is not verified on hardware: hardware-pending. No EXIT is
   needed after a reflash: the new firmware boots in normal run.
6. After the reflash, INFO shows the new firmware. If `STORAGE mounted=0`, ENTER again, then
   `format --yes-erase-partition`, then `import --from <folder>`, then verify with `list`, then EXIT.
7. Abort path (no reflash, or a failed step): EXIT (`scene_transfer.py exit --port COMx`). The ACK means the
   device is back in normal run in place; no reboot is involved.

A second ENTER sent while the first is still entering is read after the ACK and answered `N 15`. A missing
reply is not success: the device state is unknown, so send INFO before any further action.

## Verified and not verified

T14 (host, Release, CTest 35/35 in `build/native-t14`; `t14_slice` is the integration rig with
a fake LittleFS and a guarded write window):

- First boot with an empty filesystem: `ALFA` created in slot 0 (scene file, names, pointer `1\n`),
  status Succeeded, no failure counted, no fallback, window closed, KEYBRI defaults, settings file
  not written. Boot with a scene and names (and KEYBRI 0.5): loaded in place, no write. Corrupt
  scene: defaults applied, counted, file not overwritten.
- `t07b_scene_service` (named slots): create saves scene, then names, then pointer; refusals
  NameInvalid / NameTaken / NoFreeSlot; Switch, Load and CopyCurrent refused for unnamed slots;
  first-run ALFA; malformed lines skipped and counted; unreadable or oversize names never
  auto-create; stale pointer falls back to the first named slot; pointer-commit failure restores
  the names file and rolls the scene back in place; names-write failure leaves no name.
- `t13c_interrupted` (named slots): a power-cut sweep over each `request_create` (cuts 0..63):
  after reboot, LoadNames and LoadActive succeed, names are exactly the old set or the old set plus
  the new name, the new name always has its scene file, the pointer never leads its name, and an
  acknowledged create is fully visible. A first-run sweep (cuts 0..31) of the ALFA creation: every
  cut repairs on the next boot to exactly `ALFA` in slot 0.
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

Host-initiated maintenance (INFO, ENTER, EXIT; this change):

- Host-verified (`tests/native`, `t13e_run_commands` and `t14_slice`): exact tokens and golden bytes for
  INFO, ENTER, EXIT, their ACK and NAK; refusals with no change (wrong token, bad CRC, transport running,
  a note held, a scene operation in flight, name editor open, EXIT in normal run, ENTER in maintenance);
  an accepted ENTER ACKs only once Active, LIST and INFO work in maintenance, EXIT returns in place with
  scene save working afterwards and no stuck note; the `?` dump answers before, between and after
  frames, and after an abandoned partial frame; a reply queued during a streaming dump never lands inside
  it; GET of `scene_names` in maintenance; the discard and partial-frame bounds; wrap-safe gap time; and
  the INFO text for a 46-character stamped sha, a long version and 16 scenes fits its bound.
  Python: `scene_transfer.py selftest` (goldens above).
- Not hardware-verified: the ENTER round trip over the real CDC port and the host's write pattern (the
  250 ms gap), reply delivery to a host that does not read, the OLED while maintenance is entered this
  way (frozen, as for the gesture), and the timing of the quiesce (1 s bound). The quiesce-timeout NAK
  path is not exercised by a host test (it needs core 1 to stall).
- Build (pinned toolchain, `build-firmware.sh`, no upload): 0 warnings. `test-target-domain.ps1` (`-Werror
  -Wstack-usage=8192`) passes. Largest new frames in `maintenance_command.cpp`: `replay` 64 bytes, `feed` 32,
  `finish_trailer` 24. New static RAM: outbox 592 bytes and INFO text 512 bytes (the framer adds a 28-byte
  replay buffer and small state inside `EngineApp`).
