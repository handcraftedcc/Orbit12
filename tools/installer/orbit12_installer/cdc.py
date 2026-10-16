"""CDC maintenance framing, bounded status probing and the device-side allowlist.

Vendored from tools/arduino/scene_transfer.py (and firmware/Orbit12/src/persistence/
maintenance_protocol.hpp) so the one-file installer does not depend on the repository
layout. tests/installer/test_cdc.py checks the framing against the golden frames and,
when the repository copy is present, against scene_transfer.py itself.

Nothing in this module touches hardware: every exchange goes through the SerialPort
interface from interfaces.py.
"""

from __future__ import annotations

import re
import struct
import zlib
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

SYNC = bytes([0x4F, 0x31])
TYPE_LIST = ord("L")
TYPE_GET = ord("G")
TYPE_PUT = ord("P")
TYPE_ABORT = ord("X")
TYPE_FORMAT = ord("F")
TYPE_ACK = ord("A")
TYPE_NAK = ord("N")
TYPE_DATA = ord("D")
# Confirmation token; must match kFormatToken in maintenance_protocol.hpp.
FORMAT_TOKEN = b"ERASE-FS"
EXIT_BYTE = b"Q"  # plain byte outside a frame ends maintenance mode (firmware without EXIT)

# Host-initiated maintenance (Package D). INFO works in normal run and in maintenance; ENTER and
# EXIT carry the ASCII token as their name and an empty payload.
TYPE_INFO = ord("I")
TYPE_ENTER = ord("M")
TYPE_EXIT = ord("E")
ENTER_TOKEN = b"ENTER-MAINT"
EXIT_TOKEN = b"EXIT-MAINT"
INFO_MAX_BYTES = 512  # the INFO reply text is at most 512 bytes

SCENE_SLOT_COUNT = 16
SCENE_MAX_BYTES = 38637  # kSceneMaxBytes
ACTIVE_MAX_BYTES = 4  # longest pointer: "16\r\n"
USER_SETTINGS_MAX_BYTES = 128
SCENE_NAMES_MAX_BYTES = 256  # kSceneNamesMaxBytes
NAME_MAX = 16
HEADER_BYTES = 8
# Upper bound for any response payload the installer will buffer (GET of a scene is the largest).
MAX_RESPONSE_PAYLOAD = 65536

NAK_REASONS: Dict[int, str] = {
    1: "bad frame",
    2: "checksum mismatch (nothing changed)",
    3: "oversize payload (nothing changed)",
    4: "name not allowlisted",
    5: "device not stopped (maintenance refused)",
    6: "storage full (prior file kept)",
    7: "storage error (prior file kept)",
    8: "content rejected by device validation (prior file kept)",
    9: "not found on device",
    10: "format token rejected (nothing erased)",
    11: "format failed; the partition is NOT mounted",
    12: "formatted but did not mount; the partition is NOT mounted",
    13: "format verification failed; the partition is NOT mounted",
    14: "not in maintenance (the device runs normally; nothing changed)",
    15: "already in maintenance",
    16: "not stopped (playback running, a note held or output pending; nothing changed)",
    17: "core 1 did not quiesce in time (the device keeps performing)",
    18: "the device could not compose its reply (capacity)",
}
NAK_BUSY = 5
NAK_BAD_TOKEN = 10
NAK_NOT_IN_MAINTENANCE = 14
NAK_ALREADY_IN_MAINTENANCE = 15
NAK_NOT_STOPPED = 16
NAK_QUIESCE_TIMEOUT = 17

SCENE_NAMES = tuple(f"Scene{n:02d}" for n in range(1, SCENE_SLOT_COUNT + 1))
ALLOWED_NAMES = SCENE_NAMES + ("_activescene", "user_settings", "scene_names")
# Order used by restore: scenes first, names before the pointer, settings last.
RESTORE_ORDER = SCENE_NAMES + ("scene_names", "_activescene", "user_settings")

# One line per occupied slot: "NN,NAME"; NN = 01..16, NAME = 1..8 characters of [A-Z0-9].
SCENE_NAME_LINE = re.compile(r"^(0[1-9]|1[0-6]),([A-Z0-9]{1,8})\r?$")


class ProtocolError(Exception):
    """Malformed or unexpected bytes from the device."""


class DeviceRefused(Exception):
    """The device answered with a NAK (or an unexpected frame type)."""

    def __init__(self, name: str, reason: Optional[int]) -> None:
        self.name = name
        self.reason = reason
        text = NAK_REASONS.get(reason, f"reason {reason}") if reason is not None else "no reason given"
        super().__init__(f"{name}: device refused: {text}")


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_frame(frame_type: int, name: bytes = b"", payload: bytes = b"") -> bytes:
    if len(name) > NAME_MAX:
        raise ValueError("name too long")
    head = SYNC + bytes([frame_type, len(name)]) + struct.pack("<I", len(payload))
    body = head + name + payload
    return body + struct.pack("<I", crc32(body))


@dataclass(frozen=True)
class Frame:
    frame_type: int
    name: bytes
    payload: bytes

    @property
    def kind(self) -> str:
        return chr(self.frame_type)


def parse_frame(data: bytes) -> Frame:
    """Parses exactly one complete frame; raises ProtocolError otherwise."""
    if len(data) < HEADER_BYTES + 4 or data[:2] != SYNC:
        raise ProtocolError("bad sync")
    name_length = data[3]
    (payload_length,) = struct.unpack("<I", data[4:8])
    if name_length > NAME_MAX or payload_length > MAX_RESPONSE_PAYLOAD:
        raise ProtocolError("frame size out of bounds")
    if len(data) != HEADER_BYTES + name_length + payload_length + 4:
        raise ProtocolError("length mismatch")
    (crc,) = struct.unpack("<I", data[-4:])
    if crc32(data[:-4]) != crc:
        raise ProtocolError("checksum mismatch")
    name = data[HEADER_BYTES:HEADER_BYTES + name_length]
    payload = data[HEADER_BYTES + name_length:-4]
    return Frame(data[2], name, payload)


def check_scene_names(payload: bytes) -> None:
    """Raises ValueError unless every line parses and slots and names are unique."""
    text = payload.decode("ascii")
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


def validate_local(name: str, payload: bytes) -> None:
    """Client-side bounds check before a PUT; the device still validates content."""
    if name not in ALLOWED_NAMES:
        raise ValueError(f"{name} is not an allowlisted maintenance name")
    if len(payload) > max_payload(name):
        raise ValueError(f"{name}: {len(payload)} bytes exceeds the {max_payload(name)}-byte limit")
    if name == "_activescene":
        text = payload.decode("ascii", "replace")
        if text.rstrip("\r\n") not in {str(n) for n in range(1, SCENE_SLOT_COUNT + 1)}:
            raise ValueError(f"_activescene must hold a scene number 1..{SCENE_SLOT_COUNT}")
    if name == "scene_names":
        check_scene_names(payload)


# ---- INFO reply (Package D) -----------------------------------------------------------------
# Reply type 'D', empty name, ASCII text of at most 512 bytes, one section per line:
#   INFO fw=<ver> sha=<sha> api=<n> board=<id>
#   MODULES count=<n> core=<ids> user=<ids>          (ids separated by commas; empty allowed)
#   STORAGE mounted=<0|1> scenes=<n> names_ok=<0|1>
#   STATE maint=<0|1> transport=<0|1>
# Unknown sections and keys are ignored; a missing required key or a repeated section is an error.

@dataclass(frozen=True)
class InfoReport:
    fw: str
    sha: str
    api: int
    board: str
    module_count: int
    core_modules: Tuple[str, ...]
    user_modules: Tuple[str, ...]
    storage_mounted: bool
    scenes: int
    names_ok: bool
    maintenance: bool
    transport: bool


_INFO_SECTIONS = {
    "INFO": ("fw", "sha", "api", "board"),
    "MODULES": ("count",),
    "STORAGE": ("mounted", "scenes", "names_ok"),
    "STATE": ("maint", "transport"),
}


def _bit(value: str, key: str) -> bool:
    if value not in ("0", "1"):
        raise ProtocolError(f"INFO {key}={value!r} is not 0 or 1")
    return value == "1"


def _count(value: str, key: str) -> int:
    if not value.isdigit():
        raise ProtocolError(f"INFO {key}={value!r} is not a number")
    return int(value)


def parse_info(payload: bytes) -> InfoReport:
    if len(payload) > INFO_MAX_BYTES:
        raise ProtocolError("INFO reply is longer than 512 bytes")
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError:
        raise ProtocolError("INFO reply is not ASCII") from None
    sections: Dict[str, Dict[str, str]] = {}
    for line in text.splitlines():
        words = line.split()
        if not words:
            continue
        name = words[0]
        if name not in _INFO_SECTIONS:
            continue  # unknown section: ignored
        if name in sections:
            raise ProtocolError(f"INFO section {name} appears twice")
        fields: Dict[str, str] = {}
        for token in words[1:]:
            key, sep, value = token.partition("=")
            if not sep:
                raise ProtocolError(f"INFO {name}: token {token!r} is not key=value")
            fields[key] = value
        sections[name] = fields
    for name, keys in _INFO_SECTIONS.items():
        if name not in sections:
            raise ProtocolError(f"INFO reply has no {name} line")
        for key in keys:
            if key not in sections[name]:
                raise ProtocolError(f"INFO {name} has no {key}=")
    info, modules = sections["INFO"], sections["MODULES"]
    storage, state = sections["STORAGE"], sections["STATE"]
    api = _count(info["api"], "api")

    def ids(value: str) -> Tuple[str, ...]:
        return tuple(item for item in value.split(",") if item)

    return InfoReport(
        fw=info["fw"], sha=info["sha"], api=api, board=info["board"],
        module_count=_count(modules["count"], "count"),
        core_modules=ids(modules.get("core", "")), user_modules=ids(modules.get("user", "")),
        storage_mounted=_bit(storage["mounted"], "mounted"),
        scenes=_count(storage["scenes"], "scenes"),
        names_ok=_bit(storage["names_ok"], "names_ok"),
        maintenance=_bit(state["maint"], "maint"),
        transport=_bit(state["transport"], "transport"),
    )


def request_info(link: "MaintenanceLink") -> InfoReport:
    """INFO round trip. Raises ProtocolError, TimeoutError or DeviceRefused on anything but a D reply."""
    frame = link.exchange(TYPE_INFO)
    if frame.frame_type != TYPE_DATA:
        raise _refusal("INFO", frame)
    return parse_info(frame.payload)


# ---- Device status (normal mode '?' dump) -------------------------------------------------
# Firmware contract (Package A): the dump's VERSION line is exactly
#   VERSION fw=<version> git=<sha> engine_api=<n> board=<id>
# with key=value pairs. <version> is e.g. 0.1.0-pre.1 or 0.0.0-dev; <sha> may end in -dirty.
# Firmware without the line is tolerated (a warning, not a failure).

_STORAGE_LINE = re.compile(r"^STORAGE mounted=([01])", re.MULTILINE)
_VERSION_LINE = re.compile(r"^VERSION ([^\r\n]*)$", re.MULTILINE)
_REQUIRED_VERSION_KEYS = ("fw", "git", "engine_api", "board")


@dataclass(frozen=True)
class VersionInfo:
    semver: str   # fw=
    git_sha: str  # git= (may end in -dirty)
    api: int      # engine_api=
    board: str    # board=


@dataclass(frozen=True)
class DeviceStatus:
    """What a '?' dump says. orbit12 is False for anything that does not print the dump."""

    orbit12: bool
    storage_mounted: Optional[bool]
    version: Optional[VersionInfo]
    circuitpython: bool = False  # the reply looks like a CircuitPython REPL: never send frames to it
    version_problem: Optional[str] = None  # set when a VERSION line exists but cannot be read


def parse_version_line(line: str) -> VersionInfo:
    """Parses the key=value body of a VERSION line. Raises ValueError with the reason."""
    fields = {}
    for token in line.split():
        key, sep, value = token.partition("=")
        if not sep or not value or key in fields:
            raise ValueError(f"malformed token {token!r}")
        fields[key] = value
    missing = [key for key in _REQUIRED_VERSION_KEYS if key not in fields]
    if missing:
        raise ValueError("missing " + ", ".join(missing))
    if not fields["engine_api"].isdigit():
        raise ValueError(f"engine_api={fields['engine_api']!r} is not a number")
    return VersionInfo(fields["fw"], fields["git"], int(fields["engine_api"]), fields["board"])


def parse_dump(text: str) -> DeviceStatus:
    storage = _STORAGE_LINE.search(text)
    orbit12 = storage is not None or "ORBIT12-SLICE" in text
    version = None
    problem = None
    match = _VERSION_LINE.search(text)
    if match is not None:
        try:
            version = parse_version_line(match.group(1))
        except ValueError as error:
            problem = f"VERSION line not understood ({error})"
    mounted = None if storage is None else storage.group(1) == "1"
    circuitpython = not orbit12 and ("CircuitPython" in text or ">>>" in text)
    return DeviceStatus(orbit12=orbit12, storage_mounted=mounted, version=version,
                        circuitpython=circuitpython, version_problem=problem)


def read_dump(port, clock, quiet_s: float = 0.3, deadline_s: float = 3.0,
              max_bytes: int = 65536) -> str:
    """Sends '?' and reads the dump until the port goes quiet. Bounded by bytes and time."""
    port.reset_input()
    port.write(b"?")
    buffer = bytearray()
    start = clock.monotonic()
    last_data = start
    while len(buffer) < max_bytes:
        now = clock.monotonic()
        if now - start >= deadline_s:
            break
        chunk = port.read(256)
        if chunk:
            buffer += chunk
            last_data = clock.monotonic()
        elif buffer and clock.monotonic() - last_data >= quiet_s:
            break
    return bytes(buffer[:max_bytes]).decode("utf-8", "replace")


# ---- Maintenance client -------------------------------------------------------------------

def read_exact(port, count: int) -> bytes:
    out = bytearray()
    while len(out) < count:
        chunk = port.read(count - len(out))
        if not chunk:
            raise TimeoutError("device stopped responding")
        out += chunk
    return bytes(out)


def read_frame(port, max_skip: int = 65536) -> Frame:
    """Scans for sync bytes (bounded) and reads exactly one frame."""
    skipped = 0
    while True:
        first = port.read(1)
        if not first:
            raise TimeoutError("device stopped responding")
        if first != SYNC[:1]:
            skipped += 1
            if skipped > max_skip:
                raise ProtocolError("no frame sync within the scan limit")
            continue
        if read_exact(port, 1) != SYNC[1:]:
            continue
        head = read_exact(port, 6)
        name_length = head[1]
        (payload_length,) = struct.unpack("<I", head[2:6])
        if name_length > NAME_MAX or payload_length > MAX_RESPONSE_PAYLOAD:
            raise ProtocolError("response frame exceeds the installer's bounds")
        rest = read_exact(port, name_length + payload_length + 4)
        return parse_frame(SYNC + head + rest)


class MaintenanceLink:
    """One request/response at a time over a SerialPort. Never retries silently."""

    def __init__(self, port) -> None:
        self._port = port

    def exchange(self, frame_type: int, name: bytes = b"", payload: bytes = b"") -> Frame:
        self._port.reset_input()
        self._port.write(build_frame(frame_type, name, payload))
        return read_frame(self._port)

    def exit(self) -> None:
        self._port.write(EXIT_BYTE)


def parse_listing(payload: bytes) -> Dict[str, bool]:
    """LIST payload: one 'Name 0|1' line per allowlisted file."""
    result: Dict[str, bool] = {}
    for line in payload.decode("ascii", "replace").splitlines():
        if not line.strip():
            continue
        parts = line.split(" ")
        if len(parts) != 2 or parts[0] not in ALLOWED_NAMES or parts[1] not in ("0", "1"):
            raise ProtocolError(f"unexpected LIST line: {line!r}")
        result[parts[0]] = parts[1] == "1"
    return result


def list_files(link: MaintenanceLink) -> Dict[str, bool]:
    frame = link.exchange(TYPE_LIST)
    if frame.frame_type == TYPE_DATA:
        return parse_listing(frame.payload)
    raise _refusal("LIST", frame)


def get_file(link: MaintenanceLink, name: str) -> Optional[bytes]:
    """Returns the file bytes, or None when the device reports NotFound (reason 9)."""
    frame = link.exchange(TYPE_GET, name.encode("ascii"))
    if frame.frame_type == TYPE_DATA:
        return frame.payload
    if frame.frame_type == TYPE_NAK and frame.payload[:1] == bytes([9]):
        return None
    raise _refusal(name, frame)


def put_file(link: MaintenanceLink, name: str, payload: bytes) -> None:
    validate_local(name, payload)
    frame = link.exchange(TYPE_PUT, name.encode("ascii"), payload)
    if frame.frame_type != TYPE_ACK:
        raise _refusal(name, frame)


def format_partition(link: MaintenanceLink) -> None:
    """Sends the FORMAT frame. The caller must have obtained explicit confirmation."""
    frame = link.exchange(TYPE_FORMAT, FORMAT_TOKEN)
    if frame.frame_type != TYPE_ACK:
        raise _refusal("FORMAT", frame)


def _refusal(name: str, frame: Frame) -> DeviceRefused:
    """Every reply that is not the expected one is a refusal; a NAK carries its reason."""
    if frame.frame_type == TYPE_NAK:
        return DeviceRefused(name, frame.payload[0] if frame.payload else None)
    return DeviceRefused(name, None)
