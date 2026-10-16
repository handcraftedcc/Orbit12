"""Shared fakes and builders for the installer tests. No hardware, no network, no real sleeping.

FakeWorld models one or more boards: its ports, bootloader volumes and clock. FakeBoard follows
the firmware behaviour documented in docs/migration/storage.md (performance mode answers '?'
only; maintenance answers frames and the plain Q byte and ignores '?'; the 1200-baud touch enters
the bootloader; a complete UF2 copied to the RPI-RP2 drive reboots the board). The maintenance
frames are encoded here with zlib directly, so the tests do not reuse the code under test to
build their own inputs.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
import zlib
from typing import Dict, List, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
INSTALLER = os.path.join(REPO, "tools", "installer")
if INSTALLER not in sys.path:
    sys.path.insert(0, INSTALLER)

from orbit12_installer import cdc  # noqa: E402
from orbit12_installer.device import Hardware  # noqa: E402
from orbit12_installer.github import ReleaseError  # noqa: E402
from orbit12_installer.interfaces import (Clock, Console, Downloader, PortEnumerator,  # noqa: E402
                                          PortInfo, SerialOpener, SerialPort, Volume, VolumeFinder)

FAMILY = 0xE48BFF56
FLASH_START = 0x10000000
FS_START = 0x106FF000   # real clean build: 1 MiB filesystem partition
FS_SIZE = 0x100000
ADDRESS_END = 0x10043000  # real clean build: image end (exclusive)
VID = 0x239A
BOARD = "adafruit_macropad_rp2040"
NEW_VERSION = "0.1.0-pre.1"
NEW_GIT = "abcdef123456"
OLD_VERSION = "0.0.9"
OLD_GIT = "1111111aaaa"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_uf2(addresses=None, *, family: int = FAMILY, flags: int = 0x2000,
             block_count: Optional[int] = None) -> bytes:
    """Builds a UF2 with 256-byte payloads. Default: four blocks at the flash base."""
    if addresses is None:
        addresses = [FLASH_START + 256 * i for i in range(4)]
    count = len(addresses) if block_count is None else block_count
    out = bytearray()
    for index, address in enumerate(addresses):
        payload = bytes([(index * 7 + 1) & 0xFF]) * 256
        head = struct.pack("<8I", 0x0A324655, 0x9E5D5157, flags, address, len(payload), index, count, family)
        out += head + payload.ljust(476, b"\x00") + struct.pack("<I", 0x0AB16F30)
    return bytes(out)


def manifest_doc(image: bytes, /, version: str = NEW_VERSION, **overrides) -> bytes:
    doc = {
        "schema": 1, "product": "orbit12", "version": version, "prerelease": True,
        "git_sha": NEW_GIT, "engine_api": 1, "board": BOARD, "chip": "RP2040",
        "uf2_family_id": "0xe48bff56",
        "image": {"file": f"Orbit12-{version}.uf2", "sha256": sha256(image), "size": len(image),
                  "address_start": "0x10000000", "address_end": "0x10043000",
                  "blocks": len(image) // 512},
        "filesystem": {"start": "0x106ff000", "size": FS_SIZE},
        "modules": {"core": ["transpose"], "user": []},
        "repo": "handcraftedcc/Orbit12",
    }
    for key, value in overrides.items():
        if isinstance(value, dict) and isinstance(doc.get(key), dict):
            doc[key] = {**doc[key], **value}
        else:
            doc[key] = value
    return json.dumps(doc).encode("utf-8")


def sums_text(entries: Dict[str, str]) -> bytes:
    return "".join(f"{digest}  {name}\n" for name, digest in entries.items()).encode("utf-8")


def scenes() -> Dict[str, bytes]:
    """A small partition: two scenes, the names file, the pointer and the settings."""
    return {"Scene01": b"SCN1 one\n", "Scene03": b"SCN1 three\n",
            "scene_names": b"01,ALFA\n03,BRAVO\n", "_activescene": b"3\n",
            "user_settings": b"key_brightness,0.5\n"}


def encode_frame(frame_type: int, name: bytes = b"", payload: bytes = b"") -> bytes:
    """Independent frame encoder for the fake board (not cdc.build_frame)."""
    head = b"\x4f\x31" + bytes([frame_type, len(name)]) + struct.pack("<I", len(payload))
    body = head + name + payload
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def decode_request(data: bytes):
    """Returns (type, name, payload) or None when the bytes are not one valid frame."""
    if len(data) < 12 or data[:2] != b"\x4f\x31":
        return None
    name_length = data[3]
    (payload_length,) = struct.unpack("<I", data[4:8])
    if len(data) != 8 + name_length + payload_length + 4:
        return None
    if zlib.crc32(data[:-4]) & 0xFFFFFFFF != struct.unpack("<I", data[-4:])[0]:
        return None
    return data[2], data[8:8 + name_length], data[8 + name_length:-4]


class FakeClock(Clock):
    def __init__(self) -> None:
        self.now = 1000.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class FakeBoard:
    def __init__(self, world: "FakeWorld", device: str, *, mounted: bool = True,
                 version: Optional[str] = OLD_VERSION, git: str = OLD_GIT,
                 files: Optional[Dict[str, bytes]] = None, legacy: bool = False) -> None:
        self.world = world
        self.device = device
        self.mode = "normal"  # normal | maintenance | bootsel | gone
        self.mounted = mounted
        self.version = None if legacy else version
        self.git = git
        # Package D behaviour (docs/migration/storage.md "For the installer").
        self.info_supported = True    # False models older firmware: no INFO, no EXIT, no ENTER
        self.transport = False        # playback running: ENTER is NAKed NotStopped (16)
        self.busy_enters = 0          # ENTER attempts answered Busy (5) before the board accepts
        self.drop_enter_reply = False # ENTER takes effect but its ACK is lost in transit
        # 1200-baud touch failure: the touch raises (the port vanished), and the board reaches the
        # bootloader this many seconds later. None: the touch works normally.
        self.touch_failure_delay: Optional[float] = None
        self.deferred_bootsel_at: Optional[float] = None
        self.files: Dict[str, bytes] = dict(files or {})
        self.legacy = legacy  # CircuitPython-like: no Orbit12 dump at all
        self.volume_path: Optional[str] = None
        self.refuse_puts = False
        self.format_fails = False
        self.reboot_size: Optional[int] = None  # UF2 size that reboots the board
        self.next_version: Optional[str] = NEW_VERSION
        self.next_git: str = NEW_GIT
        self.reenumerates = True
        self.new_mount_state: Optional[bool] = None  # mounted state after the flash reboot

    # --- firmware behaviour ------------------------------------------------------------------
    def dump(self) -> bytes:
        if self.legacy:
            return b"Adafruit CircuitPython 9.2.0\r\n>>> "
        lines = ["ORBIT12-SLICE up_ms=1000 loops=5",
                 f"STORAGE mounted={1 if self.mounted else 0} mount_heap=0"]
        if self.version is not None:
            lines.append(f"VERSION fw={self.version} git={self.git} engine_api=1 board={BOARD}")
        return ("\n".join(lines) + "\n").encode("ascii")

    def info_text(self) -> bytes:
        scenes = sum(1 for name in self.files if name.startswith("Scene"))
        lines = [
            f"INFO fw={self.version or '0.0.0-dev'} sha={self.git} api=1 board={BOARD}",
            "MODULES count=1 core=transpose user=",
            f"STORAGE mounted={1 if self.mounted else 0} scenes={scenes} names_ok=1",
            f"STATE maint={1 if self.mode == 'maintenance' else 0} transport={1 if self.transport else 0}",
        ]
        return ("\n".join(lines)).encode("ascii")

    def on_write(self, data: bytes, port: "FakePort") -> None:
        if self.mode in ("normal", "maintenance") and data == b"?":
            if self.mode == "normal":
                port.rx += self.dump()  # the dump is served in performance mode only
            return
        request = decode_request(data)
        if request is not None and request[0] == ord("I") and self.info_supported:
            self.world.log.append(f"{self.device}:INFO")
            port.rx += encode_frame(ord("D"), b"", self.info_text())  # works in both modes
            return
        if self.mode == "normal" and data == b"Q":
            return
        if self.mode == "normal":
            if request is None or not self.info_supported:
                return  # older firmware ignores frames in performance mode
            # Current firmware: data requests are refused in performance mode (NAK 14).
            if request[0] in (ord("L"), ord("G"), ord("P"), ord("X"), ord("F")):
                port.rx += encode_frame(ord("N"), request[1], bytes([14]))
                return
            if request[0] == ord("E"):
                port.rx += encode_frame(ord("N"), request[1], bytes([14]))
                return
            if request[0] == ord("M"):
                self._enter(request[1], port)
                return
            port.rx += encode_frame(ord("N"), request[1], bytes([1]))
            return
        # maintenance mode
        if data == b"Q":
            self.mode = "normal"
            self.world.log.append(f"{self.device}:Q")
            return
        if request is None:
            port.rx += encode_frame(ord("N"), b"", bytes([1]))
            return
        frame_type, name, payload = request
        text = name.decode("ascii", "replace")
        if frame_type == ord("E") and self.info_supported:
            self.world.log.append(f"{self.device}:EXIT")
            self.mode = "normal"
            port.rx += encode_frame(ord("A"), b"EXIT-MAINT")
            return
        if frame_type == ord("M"):
            port.rx += encode_frame(ord("N"), name, bytes([15]))  # AlreadyInMaintenance
            return
        if frame_type == ord("L"):
            if not self.mounted:
                port.rx += encode_frame(ord("N"), b"", bytes([7]))
                return
            listing = "".join(f"{n} {1 if n in self.files else 0}\n" for n in cdc.ALLOWED_NAMES)
            port.rx += encode_frame(ord("D"), b"", listing.encode("ascii"))
        elif frame_type == ord("G"):
            self.world.log.append(f"{self.device}:GET:{text}")
            if not self.mounted:
                port.rx += encode_frame(ord("N"), name, bytes([7]))
            elif text not in self.files:
                port.rx += encode_frame(ord("N"), name, bytes([9]))
            else:
                port.rx += encode_frame(ord("D"), name, self.files[text])
        elif frame_type == ord("P"):
            self.world.log.append(f"{self.device}:PUT:{text}")
            if self.refuse_puts or not self.mounted:
                port.rx += encode_frame(ord("N"), name, bytes([7]))
            else:
                self.files[text] = payload
                port.rx += encode_frame(ord("A"), name)
        elif frame_type == ord("F"):
            self.world.log.append(f"{self.device}:FORMAT")
            if name != b"ERASE-FS" or payload:
                port.rx += encode_frame(ord("N"), name, bytes([10]))
            elif self.format_fails:
                self.mounted = False
                port.rx += encode_frame(ord("N"), name, bytes([11]))
            else:
                self.files.clear()
                self.mounted = True
                port.rx += encode_frame(ord("A"), name)
        else:
            port.rx += encode_frame(ord("N"), name, bytes([1]))

    def _enter(self, name: bytes, port: "FakePort") -> None:
        if name != b"ENTER-MAINT":
            port.rx += encode_frame(ord("N"), name, bytes([10]))  # BadToken
            return
        self.world.log.append(f"{self.device}:ENTER")
        if self.busy_enters > 0:
            self.busy_enters -= 1
            port.rx += encode_frame(ord("N"), name, bytes([5]))  # Busy
            return
        if self.transport:
            port.rx += encode_frame(ord("N"), name, bytes([16]))  # NotStopped
            return
        self.mode = "maintenance"
        if not self.drop_enter_reply:
            port.rx += encode_frame(ord("A"), name)

    def poll_deferred(self, now: float) -> None:
        """Brings the board into the bootloader once a failed touch's delay has passed."""
        if self.deferred_bootsel_at is not None and now >= self.deferred_bootsel_at:
            self.deferred_bootsel_at = None
            self.mode = "normal"
            self.touch()

    def touch(self) -> None:
        if self.mode in ("normal", "maintenance"):
            self.mode = "bootsel"
            self.volume_path = tempfile.mkdtemp(prefix="fake-rpi-rp2-")
            with open(os.path.join(self.volume_path, "INFO_UF2.TXT"), "w", encoding="ascii") as handle:
                handle.write("UF2 Bootloader\nModel: Raspberry Pi RP2\nBoard-ID: RPI-RP2\n")
            self.world.log.append(f"{self.device}:touch")

    def poll_reboot(self) -> None:
        """Called when the drive is listed: a complete UF2 reboots the board into the new firmware."""
        if self.mode != "bootsel" or self.volume_path is None or self.reboot_size is None:
            return
        target = os.path.join(self.volume_path, "Orbit12.uf2")
        if not os.path.exists(target) or os.path.getsize(target) != self.reboot_size:
            return
        self.world.log.append(f"{self.device}:reboot")
        shutil.rmtree(self.volume_path, ignore_errors=True)
        self.volume_path = None
        self.legacy = False  # the Orbit12 firmware replaces CircuitPython on reboot
        self.version = self.next_version
        self.git = self.next_git
        if self.new_mount_state is not None:
            self.mounted = self.new_mount_state
        self.mode = "normal" if self.reenumerates else "gone"


class FakePort(SerialPort):
    def __init__(self, board: FakeBoard, clock: FakeClock) -> None:
        self.board = board
        self.clock = clock
        self.rx = bytearray()
        self.closed = False

    def read(self, count: int) -> bytes:
        if not self.rx:
            self.clock.sleep(0.1)  # a timed-out read costs its timeout
            return b""
        out = bytes(self.rx[:count])
        del self.rx[:count]
        return out

    def write(self, data: bytes) -> None:
        self.board.on_write(data, self)

    def reset_input(self) -> None:
        self.rx.clear()

    def touch_1200(self) -> None:
        if self.board.touch_failure_delay is not None:
            # Windows reports the vanished port as an exception, but the reset still happens.
            self.board.mode = "gone"
            self.board.deferred_bootsel_at = self.clock.monotonic() + self.board.touch_failure_delay
            self.closed = True
            raise PermissionError(13, "A device attached to the system is not functioning")
        self.board.touch()
        self.close()

    def close(self) -> None:
        self.closed = True


class FakeWorld(PortEnumerator, SerialOpener, VolumeFinder):
    def __init__(self, clock: Optional[FakeClock] = None) -> None:
        self.clock = clock or FakeClock()
        self.opened: List["FakePort"] = []
        self.boards: Dict[str, FakeBoard] = {}
        self.log: List[str] = []
        self.extra_ports: List[PortInfo] = []
        self.extra_volumes: List[Volume] = []

    def add(self, device: str, **kwargs) -> FakeBoard:
        board = FakeBoard(self, device, **kwargs)
        self.boards[device] = board
        return board

    def list_ports(self) -> List[PortInfo]:
        for board in self.boards.values():
            board.poll_deferred(self.clock.monotonic())
        ports = [PortInfo(d, VID, 0x0001, "Orbit12 MacroPad") for d, b in self.boards.items()
                 if b.mode in ("normal", "maintenance")]
        return ports + list(self.extra_ports)

    def open(self, device: str, timeout_s: float) -> SerialPort:
        board = self.boards.get(device)
        if board is None or board.mode not in ("normal", "maintenance"):
            raise OSError(f"no such port {device}")
        port = FakePort(board, self.clock)
        self.opened.append(port)
        return port

    def list_volumes(self) -> List[Volume]:
        volumes = []
        for board in self.boards.values():
            board.poll_deferred(self.clock.monotonic())
            board.poll_reboot()
            if board.mode == "bootsel" and board.volume_path:
                files = frozenset(os.listdir(board.volume_path))
                with open(os.path.join(board.volume_path, "INFO_UF2.TXT"), encoding="ascii") as handle:
                    info = handle.read()
                volumes.append(Volume(board.volume_path, info, files))
        return volumes + list(self.extra_volumes)


class RecordingConsole(Console):
    """Scripted answers. Typed answers and y/n answers are consumed in order."""

    def __init__(self, *, typed: Optional[List[str]] = None, yes: Optional[List[bool]] = None,
                 choices: Optional[List[Optional[int]]] = None, asks: Optional[List[str]] = None,
                 on_pause=None, assume_yes: bool = False) -> None:
        self.typed = list(typed or [])
        self.yes = list(yes or [])
        self.choices = list(choices or [])
        self.asks = list(asks or [])
        self.on_pause = on_pause
        self.assume_yes = assume_yes
        self.said: List[str] = []
        self.warned: List[str] = []
        self.typed_prompts: List[str] = []
        self.pauses: List[str] = []

    def say(self, message: str) -> None:
        self.said.append(message)

    def warn(self, message: str) -> None:
        self.warned.append(message)

    def pause_for_action(self, message: str) -> None:
        self.pauses.append(message)
        if self.on_pause is not None:
            self.on_pause(message)

    def confirm(self, message: str, default: bool = False) -> bool:
        if self.assume_yes:
            return True
        if self.yes:
            return self.yes.pop(0)
        return default

    def confirm_typed(self, word: str, message: str) -> bool:
        self.typed_prompts.append(word)
        if self.typed:
            return self.typed.pop(0) == word
        return False

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:
        return self.choices.pop(0) if self.choices else None

    def ask(self, prompt: str) -> str:
        return self.asks.pop(0) if self.asks else ""


class FakeDownloader(Downloader):
    def __init__(self, responses: Dict[str, bytes]) -> None:
        self.responses = responses
        self.requests: List[tuple] = []

    def fetch(self, url: str, max_bytes: int, token: Optional[str] = None,
              accept: str = "application/octet-stream") -> bytes:
        self.requests.append((url, token))
        if url not in self.responses:
            raise ReleaseError(f"not found: {url}")
        data = self.responses[url]
        if len(data) > max_bytes:
            raise ReleaseError(f"download exceeds the {max_bytes}-byte limit")
        return data


def write_backup(root: str, *, age_s: float = 60.0, identity: bool = True, board: str = BOARD,
                 api: int = 1, files: Optional[Dict[str, bytes]] = None) -> str:
    """Writes a backup folder in the same layout backup_device uses (backup.json written last)."""
    import datetime as dt
    os.makedirs(root, exist_ok=True)
    created = (dt.datetime.now() - dt.timedelta(seconds=age_s)).strftime("%Y%m%d-%H%M%S")
    folder = tempfile.mkdtemp(prefix=f"backup-{created}-", dir=root)
    entries = []
    for name, data in (files if files is not None else scenes()).items():
        with open(os.path.join(folder, name), "wb") as handle:
            handle.write(data)
        entries.append({"name": name, "size": len(data), "sha256": sha256(data)})
    doc = {"schema": 1, "source": "orbit12-maintenance", "created": created, "files": entries}
    if identity:
        doc["firmware"] = {"fw": OLD_VERSION, "sha": OLD_GIT, "api": api, "board": board}
    with open(os.path.join(folder, "backup.json"), "w", encoding="utf-8") as handle:
        json.dump(doc, handle)
    return folder


def hardware(world: FakeWorld, console: Console) -> Hardware:
    return Hardware(ports=world, opener=world, volumes=world, clock=world.clock, console=console)


class TempDirTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="orbit12-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
