#!/usr/bin/env python3
"""Host side of the Orbit12 CDC maintenance protocol (T13).

Exports and imports the sixteen named scene slots (Scene01..Scene16), the scene
names file (scene_names), the active-scene pointer and user settings over the
device's USB CDC serial port while the device is stopped. The frame
format matches firmware/Orbit12/src/persistence/maintenance_protocol.hpp:

    sync 0x4F 0x31 | type u8 | name_len u8 | payload_len u32 LE | name | payload | crc32 u32 LE

The CRC-32 is zlib's (IEEE). The device acknowledges a PUT only after the
payload was written to a temp file, verified, and renamed over the target.

Usage:
    python scene_transfer.py selftest
    python scene_transfer.py list   --port COM5
    python scene_transfer.py export --port COM5 --out DIR [--force]
    python scene_transfer.py import --port COM5 --from DIR [--only NAME ...] [--dry-run]
    python scene_transfer.py format --port COM5 --yes-erase-partition
    python scene_transfer.py info   --port COM5
    python scene_transfer.py enter  --port COM5
    python scene_transfer.py exit   --port COM5

INFO prints the firmware identity and state (works in normal run and in maintenance). ENTER
asks a running device to enter maintenance; it is refused unless the engine is stopped, and
the device ACKs only once maintenance is active. EXIT returns a maintenance device to normal
run in place. Neither needs a pad held.

FORMAT erases the whole LittleFS partition (every file, including scenes and user
settings). It needs the device in maintenance mode and the flag above; it prints what
it erases, waits for the device's ACK (sent only after the format is verified), then
lists the partition to confirm.

Requires pyserial for the port commands (pip install pyserial). selftest needs
only the standard library.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import zlib

SYNC = bytes([0x4F, 0x31])
TYPE_LIST = ord("L")
TYPE_GET = ord("G")
TYPE_PUT = ord("P")
TYPE_ABORT = ord("X")
TYPE_ACK = ord("A")
TYPE_NAK = ord("N")
TYPE_DATA = ord("D")
TYPE_FORMAT = ord("F")
# Confirmation token: must match kFormatToken in maintenance_protocol.hpp (8 bytes).
FORMAT_TOKEN = b"ERASE-FS"
# Format waits this long per response: erase and verify take several seconds on flash.
FORMAT_TIMEOUT_S = 60.0
TYPE_INFO = ord("I")
TYPE_ENTER = ord("M")
TYPE_EXIT = ord("E")
# Host-initiated maintenance tokens: must match kEnterToken and kExitToken in maintenance_protocol.hpp.
ENTER_TOKEN = b"ENTER-MAINT"
EXIT_TOKEN = b"EXIT-MAINT"
# Enter waits for core 1 to quiesce (device bound 1 s); allow for the response and the ACK.
ENTER_TIMEOUT_S = 5.0

# Must match maintenance_protocol.cpp and scene_schema.hpp / user_settings.hpp.
SCENE_MAX_BYTES = 38637          # kSceneMaxBytes: 243 lines x 159 bytes (incl. 76 input bank lines)
SCENE_SLOT_COUNT = 16          # kSceneSlotCount: Scene01..Scene16, 1-based
ACTIVE_MAX_BYTES = 4           # longest pointer: "16\r\n"
USER_SETTINGS_MAX_BYTES = 128
SCENE_NAMES_MAX_BYTES = 256    # kSceneNamesMaxBytes
SCENE_NAME_MAX_CHARS = 8       # names are 1..8 characters from A-Z and 0-9
NAME_MAX = 16
HEADER_BYTES = 8

NAK_REASONS = {
    1: "bad frame",
    2: "checksum mismatch (nothing changed)",
    3: "oversize payload (nothing changed)",
    4: "name not allowlisted",
    5: "device not stopped (maintenance refused)",
    6: "storage full (prior file kept)",
    7: "storage error (prior file kept)",
    8: "content rejected by device validation (prior file kept)",
    9: "not found on device",
    10: "token rejected (nothing changed or erased)",
    11: "format failed; the partition is NOT mounted",
    12: "formatted but did not mount; the partition is NOT mounted",
    13: "format verification failed; the partition is NOT mounted",
    14: "not in maintenance (the device runs normally; nothing changed)",
    15: "already in maintenance",
    16: "not stopped (transport running, a note held or output pending; nothing changed)",
    17: "core 1 did not quiesce in time (the device keeps performing)",
    18: "the device could not compose its reply (capacity)",
}

ALLOWED = (
    [f"Scene{n:02d}" for n in range(1, SCENE_SLOT_COUNT + 1)]
    + ["_activescene", "user_settings", "scene_names"]
)

# One line per occupied slot: "NN,NAME" with NN = 01..16 and NAME = 1..8 chars [A-Z0-9].
# Blank lines are ignored; CRLF is accepted. Mirrors SceneNames parsing on the device.
SCENE_NAME_LINE = re.compile(r"^(0[1-9]|1[0-6]),([A-Z0-9]{1,8})\r?$")


def check_scene_names(payload: bytes) -> None:
    """Raises ValueError unless every line parses and slots and names are unique."""
    text = payload.decode("ascii")  # non-ASCII is a malformed names file
    slots, names = set(), set()
    for line in text.split("\n"):
        if line.strip("\r") == "":
            continue
        match = SCENE_NAME_LINE.match(line)
        if match is None:
            raise ValueError(f"scene_names line is malformed: {line!r}")
        slot, name = match.group(1), match.group(2)
        if slot in slots or name in names:
            raise ValueError(f"scene_names repeats slot or name: {line!r}")
        slots.add(slot)
        names.add(name)


def max_payload(name: str) -> int:
    if name.startswith("Scene"):
        return SCENE_MAX_BYTES
    if name == "_activescene":
        return ACTIVE_MAX_BYTES
    if name == "scene_names":
        return SCENE_NAMES_MAX_BYTES
    return USER_SETTINGS_MAX_BYTES


def build_frame(frame_type: int, name: bytes = b"", payload: bytes = b"") -> bytes:
    if len(name) > NAME_MAX:
        raise ValueError("name too long")
    head = SYNC + bytes([frame_type, len(name)]) + struct.pack("<I", len(payload))
    body = head + name + payload
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def parse_frame(data: bytes):
    """Returns (type, name, payload) for one complete frame; raises ValueError otherwise."""
    if len(data) < HEADER_BYTES + 4 or data[:2] != SYNC:
        raise ValueError("bad sync")
    frame_type = data[2]
    name_length = data[3]
    (payload_length,) = struct.unpack("<I", data[4:8])
    total = HEADER_BYTES + name_length + payload_length + 4
    if len(data) != total:
        raise ValueError("length mismatch")
    (crc,) = struct.unpack("<I", data[-4:])
    if zlib.crc32(data[:-4]) & 0xFFFFFFFF != crc:
        raise ValueError("checksum mismatch")
    name = data[HEADER_BYTES:HEADER_BYTES + name_length]
    payload = data[HEADER_BYTES + name_length:-4]
    return frame_type, name, payload


def validate_local(name: str, payload: bytes) -> None:
    """Client-side bounds check; the device still validates content."""
    if name not in ALLOWED:
        raise ValueError(f"{name} is not an allowlisted maintenance name")
    if len(payload) > max_payload(name):
        raise ValueError(f"{name}: {len(payload)} bytes exceeds the {max_payload(name)}-byte limit")
    if name == "_activescene":
        text = payload.decode("ascii", "replace")
        if text.rstrip("\r\n") not in {str(n) for n in range(1, SCENE_SLOT_COUNT + 1)}:
            raise ValueError(f"_activescene must hold a scene number 1..{SCENE_SLOT_COUNT}")
    if name == "scene_names":
        check_scene_names(payload)


def selftest() -> int:
    failures = []

    def check(condition: bool, label: str) -> None:
        if not condition:
            failures.append(label)

    # CRC-32 standard check value.
    check(zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926, "crc32 check value")
    # Golden frames: generated with zlib and checked byte-for-byte by t13a_protocol.
    golden_put = build_frame(TYPE_PUT, b"user_settings", b"key_brightness,0.5\n")
    check(
        golden_put.hex().upper() ==
        "4F3150" + "0D" + "13000000" + "757365725F73657474696E6773" +
        "6B65795F6272696768746E6573732C302E350A" + "D13B1D37",
        "golden PUT user_settings frame",
    )
    golden_get = build_frame(TYPE_GET, b"Scene01")
    check(golden_get.hex().upper() == "4F31470700000000" + "5363656E653031" + "68A9ED1D",
          "golden GET Scene01 frame")
    golden_format = build_frame(TYPE_FORMAT, FORMAT_TOKEN)
    check(golden_format.hex().upper() == "4F3146080000000045524153452D46533202957C",
          "golden FORMAT frame")
    check(parse_frame(golden_format)[1] == FORMAT_TOKEN, "FORMAT frame carries the token")
    # Host-initiated maintenance frames (goldens checked byte-for-byte by t13e_run_commands).
    golden_info = build_frame(TYPE_INFO)
    check(golden_info.hex().upper() == "4F314900000000006AFADCB3", "golden INFO frame")
    golden_enter = build_frame(TYPE_ENTER, ENTER_TOKEN)
    check(golden_enter.hex().upper() == "4F314D0B00000000454E5445522D4D41494E54A95EA0C5",
          "golden ENTER-MAINT frame")
    golden_exit = build_frame(TYPE_EXIT, EXIT_TOKEN)
    check(golden_exit.hex().upper() == "4F31450A00000000455849542D4D41494E54F2E28AB3",
          "golden EXIT-MAINT frame")
    check(build_frame(TYPE_ACK, ENTER_TOKEN).hex().upper() == "4F31410B00000000454E5445522D4D41494E54F836B850",
          "golden ACK ENTER-MAINT frame")
    check(build_frame(TYPE_NAK, ENTER_TOKEN, bytes([16])).hex().upper() ==
          "4F314E0B01000000454E5445522D4D41494E541008089424", "golden NAK NotStopped frame")
    check(parse_frame(golden_enter)[1] == ENTER_TOKEN and parse_frame(golden_exit)[1] == EXIT_TOKEN,
          "maintenance frames carry their tokens")
    check(parse_frame(golden_put)[0] == TYPE_PUT and parse_frame(golden_put)[1] == b"user_settings", "round-trip parse")
    tampered = bytearray(golden_put)
    tampered[30] ^= 1
    try:
        parse_frame(bytes(tampered))
        check(False, "tampered frame must fail checksum")
    except ValueError:
        pass
    for bad in ("../Scene01", "Scene00", "Scene17", "scene01", "Scene01.tmp", "user_settings.bak",
                "scene_names.tmp", "scene_names.bak"):
        check(bad not in ALLOWED, f"allowlist rejects {bad}")
    for good in ("Scene01", "Scene10", "Scene16", "scene_names", "_activescene", "user_settings"):
        check(good in ALLOWED, f"allowlist accepts {good}")
    try:
        validate_local("Scene01", b"x" * (SCENE_MAX_BYTES + 1))
        check(False, "oversize scene must be rejected locally")
    except ValueError:
        pass
    for pointer, ok in ((b"16\n", True), (b"16\r\n", True), (b"1\n", True), (b"17\n", False),
                        (b"0\n", False)):
        try:
            validate_local("_activescene", pointer)
            check(ok, f"pointer {pointer!r} accepted")
        except ValueError:
            check(not ok, f"pointer {pointer!r} rejected")
    names_ok = b"01,ALFA\r\n16,ZULU\n\n10,JULIET\n"
    try:
        validate_local("scene_names", names_ok)
    except ValueError as error:
        check(False, f"valid scene_names rejected: {error}")
    for names_bad in (b"01,alfa\n", b"17,ALFA\n", b"01,ALFA\n01,BRAVO\n", b"01,ALFA\n02,ALFA\n",
                      b"01,ABCDEFGHI\n", b"1,ALFA\n", b"01,ALFA\n" * 40):
        try:
            validate_local("scene_names", names_bad)
            check(False, f"scene_names must reject {names_bad[:24]!r}")
        except ValueError:
            pass
    check(max_payload("scene_names") == SCENE_NAMES_MAX_BYTES, "scene_names payload bound")

    if failures:
        for label in failures:
            print("FAIL:", label, file=sys.stderr)
        return 1
    print("scene_transfer selftest: all checks passed")
    return 0


class Link:
    """Blocking request/response over a serial port with a bounded timeout."""

    def __init__(self, port: str, timeout_s: float) -> None:
        try:
            import serial  # type: ignore
        except ImportError as error:  # pragma: no cover - environment dependent
            raise SystemExit("pyserial is required: pip install pyserial") from error
        self._serial = serial.Serial(port, baudrate=115200, timeout=timeout_s, write_timeout=timeout_s)

    def close(self) -> None:
        self._serial.close()

    def _read_exact(self, count: int) -> bytes:
        data = self._serial.read(count)
        if len(data) != count:
            raise TimeoutError("device stopped responding")
        return data

    def request(self, frame_type: int, name: bytes = b"", payload: bytes = b""):
        self._serial.reset_input_buffer()
        self._serial.write(build_frame(frame_type, name, payload))
        # Scan for sync bytes, then read exactly one response frame.
        while True:
            first = self._read_exact(1)
            if first != SYNC[:1]:
                continue
            if self._read_exact(1) != SYNC[1:]:
                continue
            head = self._read_exact(6)
            name_length = head[1]
            (payload_length,) = struct.unpack("<I", head[2:6])
            rest = self._read_exact(name_length + payload_length + 4)
            return parse_frame(SYNC + head + rest)


def describe(name: str, response) -> str:
    frame_type, _, payload = response
    if frame_type == TYPE_ACK:
        return "ACK"
    if frame_type == TYPE_NAK and payload:
        return "NAK: " + NAK_REASONS.get(payload[0], f"reason {payload[0]}")
    return f"unexpected frame type {chr(frame_type)}"


def command_list(args) -> int:
    link = Link(args.port, args.timeout)
    try:
        frame_type, _, payload = link.request(TYPE_LIST)
        if frame_type != TYPE_DATA:
            print("device refused the listing:", describe(args.port, (frame_type, b"", payload)))
            return 1
        sys.stdout.write(payload.decode("ascii", "replace"))
        return 0
    finally:
        link.close()


def command_export(args) -> int:
    os.makedirs(args.out, exist_ok=True)
    link = Link(args.port, args.timeout)
    exported = 0
    try:
        for name in ALLOWED:
            target = os.path.join(args.out, name)
            if os.path.exists(target) and not args.force:
                print(f"{name}: exists in {args.out}; use --force to replace")
                continue
            frame_type, _, payload = link.request(TYPE_GET, name.encode("ascii"))
            if frame_type == TYPE_NAK:
                reason = payload[0] if payload else 0
                if reason == 9:
                    print(f"{name}: not on device (skipped)")
                    continue
                print(f"{name}: {describe(name, (frame_type, b'', payload))}")
                return 1
            if frame_type != TYPE_DATA:
                print(f"{name}: unexpected response")
                return 1
            temp = target + ".part"
            with open(temp, "wb") as handle:
                handle.write(payload)
            os.replace(temp, target)
            exported += 1
            print(f"{name}: {len(payload)} bytes")
    finally:
        link.close()
    print(f"exported {exported} file(s) to {args.out}")
    return 0


def command_format(args) -> int:
    if not args.yes_erase_partition:
        print("format refused: this erases the whole LittleFS partition (all scenes, the active "
              "scene pointer and user settings). Re-run with --yes-erase-partition to confirm.",
              file=sys.stderr)
        return 2
    print("Erasing the LittleFS partition on", args.port + ":")
    print("  every file is removed (Scene01..Scene16, scene_names, _activescene, user_settings and any other);")
    print("  the partition is then formatted, mounted and verified by the device.")
    print("  This cannot be undone. The device must be in maintenance mode (engine stopped).")
    link = Link(args.port, max(args.timeout, FORMAT_TIMEOUT_S))
    try:
        frame_type, _, payload = link.request(TYPE_FORMAT, FORMAT_TOKEN)
    finally:
        link.close()
    if frame_type != TYPE_ACK:
        print("format NOT done:", describe("format", (frame_type, FORMAT_TOKEN, payload)))
        return 1
    print("format ACK: the device verified the new filesystem. Listing to confirm:")
    listed = command_list(args)
    if listed != 0:
        print("format was acknowledged but the listing failed; check the device.", file=sys.stderr)
    return listed


def command_info(args) -> int:
    link = Link(args.port, args.timeout)
    try:
        frame_type, _, payload = link.request(TYPE_INFO)
    finally:
        link.close()
    if frame_type != TYPE_DATA:
        print("info refused:", describe("info", (frame_type, b"", payload)))
        return 1
    sys.stdout.write(payload.decode("ascii", "replace"))
    return 0


def command_enter(args) -> int:
    link = Link(args.port, max(args.timeout, ENTER_TIMEOUT_S))
    try:
        frame_type, _, payload = link.request(TYPE_ENTER, ENTER_TOKEN)
    finally:
        link.close()
    if frame_type != TYPE_ACK:
        print("maintenance NOT entered:", describe("enter", (frame_type, ENTER_TOKEN, payload)))
        return 1
    print("maintenance entered (ACK): the engine is stopped and the storage window is open.")
    print("Run list/export/import/format now. Leave with: scene_transfer.py exit --port", args.port,
          "(or a plain Q byte, or 60 s without traffic).")
    return 0


def command_exit(args) -> int:
    link = Link(args.port, args.timeout)
    try:
        frame_type, _, payload = link.request(TYPE_EXIT, EXIT_TOKEN)
    finally:
        link.close()
    if frame_type != TYPE_ACK:
        print("exit NOT done:", describe("exit", (frame_type, EXIT_TOKEN, payload)))
        return 1
    print("returned to normal run (ACK). No reset: scenes and the pads continue in place.")
    return 0


def command_import(args) -> int:
    names = args.only or ALLOWED
    plan = []
    for name in names:
        if name not in ALLOWED:
            print(f"{name}: not allowlisted", file=sys.stderr)
            return 2
        source = os.path.join(args.source, name)
        if not os.path.isfile(source):
            print(f"{name}: not in {args.source} (skipped)")
            continue
        with open(source, "rb") as handle:
            payload = handle.read()
        try:
            validate_local(name, payload)
        except ValueError as error:
            print(f"{name}: refused locally: {error}", file=sys.stderr)
            return 2
        plan.append((name, payload))
    if args.dry_run:
        for name, payload in plan:
            print(f"would send {name} ({len(payload)} bytes)")
        return 0
    link = Link(args.port, args.timeout)
    failed = 0
    try:
        for name, payload in plan:
            response = link.request(TYPE_PUT, name.encode("ascii"), payload)
            outcome = describe(name, response)
            print(f"{name}: {outcome}")
            if response[0] != TYPE_ACK:
                failed += 1
                break  # stop on the first refusal; earlier committed files stay valid
    finally:
        link.close()
    return 1 if failed else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("selftest", help="check framing, checksum and allowlist without hardware")

    def add_port(p):
        p.add_argument("--port", required=True, help="CDC port, e.g. COM5")
        p.add_argument("--timeout", type=float, default=5.0, help="seconds per response")

    list_parser = sub.add_parser("list", help="list maintenance files on the device")
    add_port(list_parser)

    export_parser = sub.add_parser("export", help="copy device files to a host folder")
    add_port(export_parser)
    export_parser.add_argument("--out", required=True)
    export_parser.add_argument("--force", action="store_true")

    format_parser = sub.add_parser("format", help="erase and format the LittleFS partition (destructive)")
    add_port(format_parser)
    format_parser.add_argument("--yes-erase-partition", action="store_true",
                               help="required: confirms that every file on the partition is erased")

    info_parser = sub.add_parser("info", help="print firmware identity and state (normal run or maintenance)")
    add_port(info_parser)
    enter_parser = sub.add_parser("enter", help="ask a stopped device to enter maintenance (no pad held)")
    add_port(enter_parser)
    exit_parser = sub.add_parser("exit", help="return a maintenance device to normal run in place")
    add_port(exit_parser)

    import_parser = sub.add_parser("import", help="send files from a host folder to the device")
    add_port(import_parser)
    import_parser.add_argument("--from", dest="source", required=True)
    import_parser.add_argument("--only", nargs="*", help="limit to these allowlisted names")
    import_parser.add_argument("--dry-run", action="store_true")

    args = parser.parse_args(argv)
    if args.command == "selftest":
        return selftest()
    if args.command == "list":
        return command_list(args)
    if args.command == "export":
        return command_export(args)
    if args.command == "format":
        return command_format(args)
    if args.command == "info":
        return command_info(args)
    if args.command == "enter":
        return command_enter(args)
    if args.command == "exit":
        return command_exit(args)
    return command_import(args)


if __name__ == "__main__":
    sys.exit(main())
