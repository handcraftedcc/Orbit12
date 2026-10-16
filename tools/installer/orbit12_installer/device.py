"""Device operations: discovery, maintenance entry, backup, restore, format, BOOTSEL and copy.

Each function takes a Hardware bundle (interfaces only), so tests run the same code paths
against fakes. Destructive steps (format, restore over existing files) are gated here, not
only in the CLI: format_device always asks for the typed word ERASE.
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import json
import os
import re
import shutil
from dataclasses import dataclass
from typing import Callable, List, Optional, Tuple

from . import cdc
from .cdc import DeviceRefused, DeviceStatus, MaintenanceLink, ProtocolError
from .interfaces import Clock, Console, PortEnumerator, PortInfo, SerialOpener, Volume, VolumeFinder
from .package import BOARD, Manifest

# USB vendor IDs that Orbit12 boards may enumerate with: Adafruit (CircuitPython MacroPad)
# and Raspberry Pi (arduino-pico default). The exact PID of the Orbit12 build is not pinned in
# this repository, so identity is decided by the '?' dump, not by the PID.
CANDIDATE_VIDS = frozenset({0x239A, 0x2E8A})
BOOTSEL_MARKER = "RPI-RP2"
UF2_VOLUME_FILE = "Orbit12.uf2"
CIRCUITPY_MARKER_FILE = "boot_out.txt"
CIRCUITPY_USERDATA = "userdata"
CIRCUITPY_MAX_BYTES = 4 * 1024 * 1024
OPEN_TIMEOUT_S = 0.1
PROBE_TIMEOUT_S = 1.0  # discovery: other CDC devices must not stall the scan
REQUEST_TIMEOUT_S = 5.0


class FlashError(Exception):
    """A step failed; the message says what the user should do next."""


class Cancelled(Exception):
    """The user declined a step."""


@dataclass
class Hardware:
    ports: PortEnumerator
    opener: SerialOpener
    volumes: VolumeFinder
    clock: Clock
    console: Console


@dataclass(frozen=True)
class Orbit12Device:
    """A board that answered as Orbit12. info is the INFO report when the firmware gives one."""

    port: PortInfo
    status: DeviceStatus
    info: Optional[cdc.InfoReport] = None

    @property
    def storage_mounted(self) -> Optional[bool]:
        """INFO's STORAGE line when present, otherwise the '?' dump's (older firmware)."""
        if self.info is not None:
            return self.info.storage_mounted
        return self.status.storage_mounted


# ---- waiting helper ------------------------------------------------------------------------

def wait_until(hw: Hardware, timeout_s: float, check: Callable[[], object],
               interval_s: float = 0.25):
    """Polls check() until it returns a truthy value or the timeout passes. Returns None on timeout."""
    deadline = hw.clock.monotonic() + timeout_s
    while True:
        result = check()
        if result:
            return result
        if hw.clock.monotonic() >= deadline:
            return None
        hw.clock.sleep(interval_s)


# ---- port discovery and probing ------------------------------------------------------------

def _candidate_ports(hw: Hardware, override: Optional[str]) -> List[PortInfo]:
    ports = hw.ports.list_ports()
    if override:
        return [p for p in ports if p.device.lower() == override.lower()] or [
            PortInfo(override, None, None, "(--port)")]
    return [p for p in ports if p.vid in CANDIDATE_VIDS]


def probe_status(hw: Hardware, device: str) -> Optional[DeviceStatus]:
    """Opens the port, sends '?', returns the parsed status. None when the port cannot be used."""
    try:
        port = hw.opener.open(device, OPEN_TIMEOUT_S)
    except OSError:
        return None
    try:
        text = cdc.read_dump(port, hw.clock)
    finally:
        port.close()
    return cdc.parse_dump(text)


def read_info(hw: Hardware, device: str, timeout_s: float = REQUEST_TIMEOUT_S) -> Optional[cdc.InfoReport]:
    """INFO round trip. None when the firmware does not answer INFO (older firmware) or cannot be read."""
    try:
        port = hw.opener.open(device, timeout_s)
    except OSError:
        return None
    try:
        return cdc.request_info(MaintenanceLink(port))
    except (TimeoutError, ProtocolError, DeviceRefused):
        return None
    finally:
        port.close()


def find_orbit12(hw: Hardware, override: Optional[str] = None) -> Optional[Orbit12Device]:
    """Exactly one Orbit12 CDC device, or None. Several matches are refused, never guessed.

    A board answers as Orbit12 when its '?' dump says so, or when it answers INFO (INFO also works
    in maintenance, where '?' is ignored). A CircuitPython REPL is never sent protocol frames.
    Older firmware in maintenance answers LIST, so that is a last resort.
    """
    found = []
    for candidate in _candidate_ports(hw, override):
        status = probe_status(hw, candidate.device)
        if status is not None and status.circuitpython:
            continue
        report = read_info(hw, candidate.device, PROBE_TIMEOUT_S)
        if (status is not None and status.orbit12) or report is not None:
            found.append(Orbit12Device(
                candidate,
                status if status is not None else DeviceStatus(orbit12=True, storage_mounted=None, version=None),
                report))
        elif maintenance_active(hw, candidate.device, timeout_s=PROBE_TIMEOUT_S):
            found.append(Orbit12Device(candidate, DeviceStatus(orbit12=True, storage_mounted=None, version=None)))
    if len(found) > 1:
        names = ", ".join(item.port.device for item in found)
        raise FlashError(f"several Orbit12 boards are connected ({names}); unplug all but one, "
                         "or pass --port to choose")
    return found[0] if found else None


def has_other_cdc(hw: Hardware) -> bool:
    return any(p.vid in CANDIDATE_VIDS for p in hw.ports.list_ports())


def lone_cdc_port(hw: Hardware) -> Optional[str]:
    """The only candidate CDC port, or None when there are none or several (then ask for BOOT)."""
    ports = [p for p in hw.ports.list_ports() if p.vid in CANDIDATE_VIDS]
    return ports[0].device if len(ports) == 1 else None


# ---- maintenance ---------------------------------------------------------------------------

ENTER_RETRY_INTERVAL_S = 2.0   # ENTER NAKed Busy / NotStopped / QuiesceTimeout: retry this often
ENTER_RETRY_WINDOW_S = 20.0    # ... for at most this long, then the key-hold step
RETRY_NAKS = frozenset({cdc.NAK_BUSY, cdc.NAK_NOT_STOPPED, cdc.NAK_QUIESCE_TIMEOUT})


def open_link(hw: Hardware, device: str) -> Tuple[object, MaintenanceLink]:
    port = hw.opener.open(device, REQUEST_TIMEOUT_S)
    return port, MaintenanceLink(port)


def maintenance_active(hw: Hardware, device: str, timeout_s: float = REQUEST_TIMEOUT_S) -> bool:
    """INFO's STATE maint=1 when the firmware answers INFO. Otherwise (older firmware) a LIST reply."""
    report = read_info(hw, device, timeout_s)
    if report is not None:
        return report.maintenance
    try:
        port = hw.opener.open(device, timeout_s)
        link = MaintenanceLink(port)
    except OSError:
        return False
    try:
        cdc.list_files(link)
        return True
    except DeviceRefused as error:
        # Older firmware refuses data requests only in maintenance (for example NAK 7 when the
        # partition is unmounted), so any refusal other than "not in maintenance" counts.
        return error.reason != cdc.NAK_NOT_IN_MAINTENANCE
    except (TimeoutError, ProtocolError):
        return False
    finally:
        port.close()


def find_maintenance_port(hw: Hardware) -> Optional[str]:
    for candidate in _candidate_ports(hw, None):
        status = probe_status(hw, candidate.device)
        if status is not None and status.circuitpython:
            continue
        if maintenance_active(hw, candidate.device):
            return candidate.device
    return None


def _enter_once(hw: Hardware, device: str) -> Tuple[str, str]:
    """One ENTER attempt. Returns (outcome, text) with outcome 'active', 'retry' or 'fail'."""
    try:
        port = hw.opener.open(device, REQUEST_TIMEOUT_S)
    except OSError as error:
        return "fail", f"could not open {device}: {error}"
    try:
        link = MaintenanceLink(port)
        try:
            frame = link.exchange(cdc.TYPE_ENTER, cdc.ENTER_TOKEN)
        except (TimeoutError, ProtocolError):
            # A missing reply is not success: the device state is unknown, so ask INFO first.
            try:
                report = cdc.request_info(link)
            except (TimeoutError, ProtocolError, DeviceRefused):
                report = None
            if report is not None and report.maintenance:
                return "active", ""
            return "retry", "no reply to ENTER"
    finally:
        port.close()
    if frame.frame_type == cdc.TYPE_ACK:
        return "active", ""
    if frame.frame_type == cdc.TYPE_NAK:
        reason = frame.payload[0] if frame.payload else None
        if reason == cdc.NAK_ALREADY_IN_MAINTENANCE:
            return "active", ""
        text = cdc.NAK_REASONS.get(reason, "no reason given")
        return ("retry" if reason in RETRY_NAKS else "fail"), text
    return "fail", "unexpected reply to ENTER"


def host_enter(hw: Hardware, device: str, transport_running: bool) -> bool:
    """ENTER with retries. True when maintenance is active. Playback must be stopped first."""
    if transport_running:
        hw.console.pause_for_action(
            "Playback is running. Stop it first: hold the encoder and press the first pad (the stop "
            "shortcut). Press Enter when playback has stopped.")
    deadline = hw.clock.monotonic() + ENTER_RETRY_WINDOW_S
    while True:
        outcome, text = _enter_once(hw, device)
        if outcome == "active":
            return True
        if outcome == "fail":
            hw.console.warn(f"the firmware refused maintenance entry: {text}")
            return False
        if hw.clock.monotonic() + ENTER_RETRY_INTERVAL_S > deadline:
            hw.console.warn(f"the firmware stayed busy ({text}) for {ENTER_RETRY_WINDOW_S:.0f} s; "
                            "falling back to the key-hold step")
            return False
        hw.console.say(f"  {text}; trying again in {ENTER_RETRY_INTERVAL_S:.0f} s")
        hw.clock.sleep(ENTER_RETRY_INTERVAL_S)


def ensure_maintenance(hw: Hardware, orbit: Orbit12Device, attempts: int = 3) -> Optional[str]:
    """Returns the device path in maintenance mode, or None after the user gives up.

    Order: already in maintenance (INFO); host ENTER when INFO answers (with stop-playback prompt
    and retries); otherwise the user holds a key while power-cycling the board.
    """
    device_path = orbit.port.device
    report = read_info(hw, device_path)
    if report is not None:
        if report.maintenance:
            return device_path
        if host_enter(hw, device_path, report.transport):
            return device_path
    elif maintenance_active(hw, device_path):  # older firmware, already in maintenance
        return device_path
    for _ in range(attempts):
        hw.console.pause_for_action(
            "Maintenance mode is needed for the scene backup. Unplug the board, then hold any key "
            "while you plug it back in and keep it held for two seconds. Release it, wait for the "
            "screen to keep its last frame, then press Enter.")
        found = find_maintenance_port(hw)
        if found is not None:
            return found
        hw.console.warn("Maintenance mode was not reached on any Orbit12 port.")
        if not hw.console.confirm("Try the power-cycle again?", default=True):
            break
    return None


def exit_maintenance(hw: Hardware, device: str) -> None:
    """Leaves maintenance in place: EXIT-MAINT when the firmware answers it, otherwise the Q byte."""
    try:
        port = hw.opener.open(device, REQUEST_TIMEOUT_S)
    except OSError as error:
        raise FlashError(f"could not reopen {device} to leave maintenance: {error}") from None
    try:
        link = MaintenanceLink(port)
        try:
            frame = link.exchange(cdc.TYPE_EXIT, cdc.EXIT_TOKEN)
        except (TimeoutError, ProtocolError):
            frame = None
        if frame is not None and frame.frame_type == cdc.TYPE_ACK:
            return
        if frame is not None and frame.frame_type == cdc.TYPE_NAK and frame.payload[:1] == bytes(
                [cdc.NAK_NOT_IN_MAINTENANCE]):
            return  # already in performance mode
        # No EXIT support (older firmware) or no reply: the documented plain Q byte.
        link.exit()
    finally:
        port.close()


# ---- backup and restore --------------------------------------------------------------------

BACKUP_MANIFEST = "backup.json"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def backup_device(hw: Hardware, device: str, root: str, identity: Optional[dict] = None) -> Optional[str]:
    """Copies every present maintenance file into a new timestamped folder under root.

    Returns the folder, or None when the storage reports nothing readable. Writes backup.json
    last, so a folder without it is incomplete and must not be restored.
    """
    port, link = open_link(hw, device)
    try:
        try:
            listing = cdc.list_files(link)
        except DeviceRefused as error:
            hw.console.warn(f"storage could not be listed ({error}); nothing was backed up")
            return None
        names = [n for n in cdc.RESTORE_ORDER if listing.get(n)]
        if not names:
            hw.console.warn("the partition holds no scene or settings files; nothing to back up")
            return None
        stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        folder = _new_folder(root, f"backup-{stamp}")
        entries = []
        for name in names:
            data = cdc.get_file(link, name)
            if data is None:
                raise FlashError(f"{name} was listed but could not be read; no backup was written")
            with open(os.path.join(folder, name), "xb") as handle:
                handle.write(data)
            entries.append({"name": name, "size": len(data), "sha256": _sha256(data)})
            hw.console.say(f"  saved {name} ({len(data)} bytes)")
    finally:
        port.close()
    doc = {"schema": 1, "source": "orbit12-maintenance", "created": stamp, "files": entries}
    if identity:
        doc["firmware"] = identity  # fw, sha, api, board of the firmware the backup came from
    with open(os.path.join(folder, BACKUP_MANIFEST), "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2)
    return folder


def _new_folder(root: str, name: str) -> str:
    os.makedirs(root, exist_ok=True)
    candidate, suffix = os.path.join(root, name), 1
    while os.path.exists(candidate):
        suffix += 1
        candidate = os.path.join(root, f"{name}-{suffix}")
    os.makedirs(candidate)
    return candidate


def load_backup(folder: str) -> List[Tuple[str, bytes]]:
    """Reads a backup folder and verifies every file against backup.json. Refuses anything else."""
    path = os.path.join(folder, BACKUP_MANIFEST)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            doc = json.load(handle)
    except (OSError, ValueError) as error:
        raise FlashError(f"{folder} is not a complete backup ({error})") from None
    if not isinstance(doc, dict) or doc.get("schema") != 1 or not isinstance(doc.get("files"), list):
        raise FlashError(f"{folder}: unsupported backup manifest")
    result = []
    for entry in doc["files"]:
        name = entry.get("name")
        if name not in cdc.ALLOWED_NAMES:
            raise FlashError(f"backup lists a name that is not allowed: {name!r}")
        with open(os.path.join(folder, name), "rb") as handle:
            data = handle.read(cdc.SCENE_MAX_BYTES + 1)
        if len(data) != entry.get("size") or _sha256(data) != entry.get("sha256"):
            raise FlashError(f"backup file {name} does not match its recorded hash; not restoring")
        result.append((name, data))
    return result


RECENT_BACKUP_S = 3600  # a backup this young may be offered when the board is already in its bootloader


def find_recent_backup(root: str, engine_api: int, max_age_s: float = RECENT_BACKUP_S) -> Optional[str]:
    """The newest backup under root that is less than max_age_s old, was taken from Orbit12 firmware
    for this board and an engine API compatible with the release, and passes its hash check.
    Compatibility is board and engine_api only: the firmware version is expected to differ."""
    if not os.path.isdir(root):
        return None
    now = _dt.datetime.now()
    best = None
    for name in os.listdir(root):
        folder = os.path.join(root, name)
        if not name.startswith("backup-") or not os.path.isfile(os.path.join(folder, BACKUP_MANIFEST)):
            continue
        try:
            with open(os.path.join(folder, BACKUP_MANIFEST), "r", encoding="utf-8") as handle:
                doc = json.load(handle)
            created = _dt.datetime.strptime(str(doc.get("created", "")), "%Y%m%d-%H%M%S")
        except (OSError, ValueError, AttributeError):
            continue
        if not isinstance(doc, dict) or doc.get("schema") != 1:
            continue
        identity = doc.get("firmware")
        if not isinstance(identity, dict) or identity.get("board") != BOARD or identity.get("api") != engine_api:
            continue
        age = (now - created).total_seconds()
        if age < -60 or age > max_age_s:
            continue
        try:
            load_backup(folder)
        except FlashError:
            continue
        if best is None or created > best[0]:
            best = (created, folder)
    return best[1] if best else None


def restore_device(hw: Hardware, device: str, folder: str) -> List[str]:
    """PUTs the backup files in RESTORE_ORDER. Stops at the first refusal and reports it."""
    files = dict(load_backup(folder))
    restored = []
    port, link = open_link(hw, device)
    try:
        for name in cdc.RESTORE_ORDER:
            if name not in files:
                continue
            try:
                cdc.put_file(link, name, files[name])
            except ValueError as error:
                raise FlashError(f"{name} in the backup is refused by the installer's checks: {error}") from None
            restored.append(name)
            hw.console.say(f"  restored {name}")
    finally:
        port.close()
    return restored


def format_device(hw: Hardware, device: str) -> None:
    """Erases the partition after a typed ERASE. The device must already be in maintenance."""
    hw.console.warn(
        "FORMAT erases EVERY file on the board's scene partition: all saved scenes, scene names, "
        "the active-scene pointer and user settings. It cannot be undone. Leftover CIRCUITPY data "
        "is lost as well unless it was copied off first.")
    if not hw.console.confirm_typed("ERASE", "Type ERASE to format the partition"):
        raise Cancelled("format cancelled; nothing was erased")
    port, link = open_link(hw, device)
    try:
        cdc.format_partition(link)
        listing = cdc.list_files(link)
    except DeviceRefused as error:
        raise FlashError(f"format was refused: {error}") from None
    finally:
        port.close()
    if any(listing.values()):
        raise FlashError("format was acknowledged but files are still listed; check the device")
    hw.console.say("Partition formatted and verified. The device is still in maintenance mode.")


# ---- BOOTSEL and volumes -------------------------------------------------------------------

def bootsel_volumes(hw: Hardware) -> List[Volume]:
    return [v for v in hw.volumes.list_volumes() if BOOTSEL_MARKER in v.info_text]


def circuitpy_volumes(hw: Hardware) -> List[Volume]:
    return [v for v in hw.volumes.list_volumes()
            if CIRCUITPY_MARKER_FILE in v.files and BOOTSEL_MARKER not in v.info_text]


TOUCH_SETTLE_S = 15.0  # after a 1200-baud touch, how long the bootloader drive may take to appear


class NoBootselDrive(FlashError):
    """No RPI-RP2 drive is present (as opposed to several, which is an ambiguity error)."""


def single_bootsel_volume(hw: Hardware) -> Optional[Volume]:
    """The one RPI-RP2 drive, None when there is none; several are refused."""
    found = bootsel_volumes(hw)
    if len(found) > 1:
        paths = ", ".join(v.path for v in found)
        raise FlashError(f"several RPI-RP2 drives are connected ({paths}); refusing to choose one")
    return found[0] if found else None


def enter_bootloader(hw: Hardware, orbit_port: Optional[str], timeout_s: float = 30.0) -> Volume:
    """Puts the board into its bootloader and returns the RPI-RP2 drive.

    With a CDC port, a 1200-baud touch is sent first. The touch is judged by its outcome, not by
    whether it raised: the board often resets as the port closes, so an exception (for example
    Windows "a device attached to the system is not functioning") is inconclusive. The drive is
    polled for up to TOUCH_SETTLE_S before the BOOT-key instruction is shown. The port is always
    closed before the touch returns, and the caller leaves maintenance before calling this.
    """
    if orbit_port is not None:
        port = None
        try:
            port = hw.opener.open(orbit_port, OPEN_TIMEOUT_S)
            port.touch_1200()
        except OSError as error:
            hw.console.say(f"  the 1200-baud touch on {orbit_port} reported: {error}; checking whether the "
                           "board reset into its bootloader")
        finally:
            if port is not None:
                port.close()
        try:
            return wait_for_bootsel(hw, TOUCH_SETTLE_S)
        except NoBootselDrive:
            hw.console.warn("the bootloader drive did not appear after the 1200-baud touch")
    hw.console.pause_for_action(
        "Put the board in its bootloader: hold the BOOT button (where it is is shown in Adafruit's "
        "MacroPad RP2040 guide) while you connect the USB cable, and release it once the RPI-RP2 "
        "drive appears.")
    return wait_for_bootsel(hw, timeout_s)


def wait_for_bootsel(hw: Hardware, timeout_s: float = 30.0) -> Volume:
    """Waits for exactly one RPI-RP2 volume. Zero after the timeout is NoBootselDrive; several is refused."""
    def check():
        found = bootsel_volumes(hw)
        if len(found) > 1:
            return ("many", found)
        return ("one", found[0]) if found else None

    result = wait_until(hw, timeout_s, check)
    if result is None:
        raise NoBootselDrive("no RP2040 bootloader drive (RPI-RP2) appeared; check the cable, or hold "
                             "BOOT while connecting")
    kind, value = result
    if kind == "many":
        paths = ", ".join(v.path for v in value)
        raise FlashError(f"several RPI-RP2 drives are connected ({paths}); refusing to choose one")
    return value


# ---- copy and verify -----------------------------------------------------------------------

class CopyInterrupted(FlashError):
    pass


@dataclass(frozen=True)
class CopyResult:
    bytes_written: int
    pending: bool  # True when the drive vanished after every byte was written (normal reboot)


def copy_image(volume: Volume, data: bytes, opener: Callable = open,
               chunk_bytes: int = 4096) -> CopyResult:
    """Writes the image as UF2_VOLUME_FILE and checks the size. Never formats or picks a drive."""
    target = os.path.join(volume.path, UF2_VOLUME_FILE)
    written = 0
    try:
        # Unbuffered: a successful write() means the OS accepted those bytes, so a failure at
        # flush or close afterwards is the bootloader resetting, not an interrupted copy.
        handle = opener(target, "wb", buffering=0)
    except OSError as error:
        raise CopyInterrupted(f"could not open {target}: {error}") from None
    try:
        view = memoryview(data)
        while written < len(data):
            piece = view[written:written + chunk_bytes]
            accepted = handle.write(piece)
            accepted = len(piece) if accepted is None else accepted
            if accepted <= 0:
                raise OSError("the drive accepted no bytes")
            written += accepted
    except OSError as error:
        handle.close()
        raise CopyInterrupted(
            f"the copy stopped after {written} of {len(data)} bytes ({error}). The board is still in "
            "the bootloader; run the install again.") from None
    try:
        handle.flush()
        try:
            os.fsync(handle.fileno())
        except (OSError, AttributeError, ValueError):
            pass  # the bootloader volume may not accept fsync; the size check below is the check
        handle.close()
    except OSError:
        # Every byte was written. The bootloader resets as soon as the last block lands, so the
        # close or flush may fail when the drive disappears. Re-enumeration decides the outcome.
        return CopyResult(written, pending=True)
    try:
        size = os.path.getsize(target)
    except OSError:
        return CopyResult(written, pending=True)
    if size != len(data):
        raise CopyInterrupted(f"the drive reports {size} bytes; expected {len(data)}. Run again.")
    return CopyResult(written, pending=False)


def wait_for_drive_gone(hw: Hardware, path: str, timeout_s: float) -> bool:
    return wait_until(hw, timeout_s,
                      lambda: not any(v.path == path for v in bootsel_volumes(hw))) is True


def wait_for_reenumeration(hw: Hardware, timeout_s: float) -> Optional[Orbit12Device]:
    """Polls until an Orbit12 CDC device answers. Several matches still raise (ambiguous)."""
    return wait_until(hw, timeout_s, lambda: find_orbit12(hw), interval_s=0.5)


def verify_firmware(dev: Orbit12Device, manifest: Manifest) -> List[str]:
    """Checks the board after the flash. INFO is primary; the '?' dump is the fallback.

    Hard mismatches raise FlashError. Missing or unreadable optional data comes back as warnings.
    """
    warnings: List[str] = []
    info = dev.info
    if info is not None:
        if info.board != BOARD:
            raise FlashError(f"INFO reports board={info.board}; expected {BOARD}")
        if info.api != manifest.engine_api:
            raise FlashError(f"INFO reports engine_api={info.api}; the release is engine_api={manifest.engine_api}")
        if info.fw != manifest.version:
            raise FlashError(f"INFO reports fw={info.fw}; expected {manifest.version}")
        base = _git_base(info.sha)
        if not (base.startswith(manifest.git_sha) or manifest.git_sha.startswith(base)):
            raise FlashError(f"INFO reports sha={info.sha}; expected {manifest.git_sha}")
        if base != info.sha:
            warnings.append("the firmware was built from a tree with uncommitted changes (-dirty)")
        return warnings
    status = dev.status
    if not status.orbit12:
        raise FlashError("the board re-enumerated but does not answer as Orbit12 firmware")
    if status.storage_mounted is None:
        warnings.append("the status dump has no STORAGE line; the partition state is unknown")
    if status.version_problem:
        warnings.append(f"{status.version_problem}; the version is not verified")
        return warnings
    if status.version is None:
        warnings.append("the firmware answers neither INFO nor a VERSION line; the version is not verified")
        return warnings
    version = status.version
    if version.board != BOARD:
        raise FlashError(f"the board reports board={version.board}; expected {BOARD}")
    if version.api != manifest.engine_api:
        raise FlashError(f"the firmware reports engine_api={version.api}; the release is engine_api={manifest.engine_api}")
    if version.semver != manifest.version:
        raise FlashError(f"the firmware reports fw={version.semver}; expected {manifest.version}")
    base = _git_base(version.git_sha)
    if not (base.startswith(manifest.git_sha) or manifest.git_sha.startswith(base)):
        raise FlashError(f"the firmware reports git={version.git_sha}; expected {manifest.git_sha}")
    if base != version.git_sha:
        warnings.append("the firmware was built from a tree with uncommitted changes (-dirty)")
    warnings.append("the firmware does not answer INFO (older firmware); verified from the dump only")
    return warnings


def _git_base(sha: str) -> str:
    return sha[:-len("-dirty")] if sha.endswith("-dirty") else sha


def copy_circuitpy_userdata(hw: Hardware, root: str) -> Optional[str]:
    """Read-only copy of CIRCUITPY/userdata into a new folder under root (never writes to the drive)."""
    volumes = circuitpy_volumes(hw)
    if len(volumes) != 1:
        return None
    source = os.path.join(volumes[0].path, CIRCUITPY_USERDATA)
    if not os.path.isdir(source):
        return None
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    destination = _new_folder(root, f"circuitpy-userdata-{stamp}")
    total = 0
    for dirpath, dirnames, filenames in os.walk(source):
        dirnames[:] = dirnames[:64]
        for filename in filenames[:512]:
            path = os.path.join(dirpath, filename)
            size = os.path.getsize(path)
            total += size
            if total > CIRCUITPY_MAX_BYTES:
                raise FlashError("CIRCUITPY userdata is larger than the installer copies; copy it by hand")
            relative = os.path.relpath(path, volumes[0].path)
            target = os.path.join(destination, relative)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(path, target)
    return destination


def default_backup_root() -> str:
    """Next to the executable when frozen; otherwise the current directory. Falls back to home."""
    import sys

    base = os.path.dirname(os.path.abspath(sys.executable)) if getattr(sys, "frozen", False) \
        else os.getcwd()
    candidate = os.path.join(base, "orbit12-backups")
    try:
        os.makedirs(candidate, exist_ok=True)
        probe = os.path.join(candidate, ".write-test")
        with open(probe, "w", encoding="utf-8") as handle:
            handle.write("ok")
        os.remove(probe)
        return candidate
    except OSError:
        fallback = os.path.join(os.path.expanduser("~"), "Orbit12-backups")
        os.makedirs(fallback, exist_ok=True)
        return fallback
