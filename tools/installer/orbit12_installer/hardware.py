"""Real implementations of the hardware seams: pyserial ports, removable volumes and the clock.

pyserial and the Windows/Linux/macOS volume APIs are imported or called only here. The rest
of the installer sees SerialPort, PortEnumerator, VolumeFinder and Clock from interfaces.py.
"""

from __future__ import annotations

import glob
import os
import sys
import time
from typing import List

from .interfaces import Clock, PortEnumerator, PortInfo, SerialOpener, SerialPort, Volume, VolumeFinder

INFO_FILE = "INFO_UF2.TXT"
MAX_INFO_BYTES = 4096
MAX_TOP_LEVEL_NAMES = 512


class RealClock(Clock):
    def monotonic(self) -> float:
        return time.monotonic()

    def sleep(self, seconds: float) -> None:
        time.sleep(seconds)


class _PySerialPort(SerialPort):
    def __init__(self, serial_module, device: str, timeout_s: float) -> None:
        self._serial_module = serial_module
        self._device = device
        self._port = serial_module.Serial(device, baudrate=115200, timeout=timeout_s,
                                          write_timeout=timeout_s)

    def read(self, count: int) -> bytes:
        return self._port.read(count)

    def write(self, data: bytes) -> None:
        self._port.write(data)
        self._port.flush()

    def reset_input(self) -> None:
        self._port.reset_input_buffer()

    def touch_1200(self) -> None:
        self._port.close()
        touch = self._serial_module.Serial()
        touch.port = self._device
        touch.baudrate = 1200
        touch.dtr = False
        touch.rts = False
        touch.open()
        touch.close()

    def close(self) -> None:
        self._port.close()


class PySerialOpener(SerialOpener):
    def open(self, device: str, timeout_s: float) -> SerialPort:
        import serial  # imported here so the module loads without pyserial for tests

        return _PySerialPort(serial, device, timeout_s)


class PySerialEnumerator(PortEnumerator):
    def list_ports(self) -> List[PortInfo]:
        from serial.tools import list_ports

        found = []
        for item in list_ports.comports():
            found.append(PortInfo(item.device, item.vid, item.pid, item.description or ""))
        return found


def _read_info(directory: str) -> str:
    path = os.path.join(directory, INFO_FILE)
    try:
        with open(path, "rb") as handle:
            return handle.read(MAX_INFO_BYTES).decode("utf-8", "replace")
    except OSError:
        return ""


def _volume_at(directory: str) -> Volume:
    try:
        names = os.listdir(directory)[:MAX_TOP_LEVEL_NAMES]
    except OSError:
        names = []
    return Volume(directory, _read_info(directory), frozenset(names))


def _windows_removable_roots() -> List[str]:
    import ctypes

    kernel = ctypes.windll.kernel32
    kernel.SetErrorMode(0x0001)  # SEM_FAILCRITICALERRORS: no "no disk" dialogs for empty readers
    drives = kernel.GetLogicalDrives()
    roots = []
    for index in range(2, 26):  # skip A: and B:
        if not drives & (1 << index):
            continue
        root = f"{chr(ord('A') + index)}:\\"
        if kernel.GetDriveTypeW(root) == 2:  # DRIVE_REMOVABLE
            roots.append(root)
    return roots


def _posix_candidate_roots() -> List[str]:
    if sys.platform == "darwin":
        return [p for p in sorted(glob.glob("/Volumes/*")) if os.path.isdir(p)]
    candidates = (glob.glob("/media/*/*") + glob.glob("/run/media/*/*")
                  + glob.glob("/media/*") + glob.glob("/mnt/*"))
    return [p for p in sorted(set(candidates)) if os.path.isdir(p)]


class SystemVolumeFinder(VolumeFinder):
    """Lists candidate volumes. Classification (RPI-RP2 vs CIRCUITPY) happens in device.py."""

    def list_volumes(self) -> List[Volume]:
        roots = _windows_removable_roots() if os.name == "nt" else _posix_candidate_roots()
        return [_volume_at(root) for root in roots]
