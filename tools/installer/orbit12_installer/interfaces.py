"""Hardware and I/O seams. Every device access in the installer goes through these.

Real implementations live in hardware.py, github.py and console.py. Tests supply fakes.
Nothing in this module imports pyserial or Arduino code.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional


@dataclass(frozen=True)
class PortInfo:
    device: str
    vid: Optional[int]
    pid: Optional[int]
    description: str


@dataclass(frozen=True)
class Volume:
    path: str
    info_text: str
    files: frozenset


class SerialPort:
    """A CDC serial port. read() returns b'' on timeout; it never blocks forever."""

    def read(self, count: int) -> bytes:  # pragma: no cover - interface
        raise NotImplementedError

    def write(self, data: bytes) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def reset_input(self) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def touch_1200(self) -> None:  # pragma: no cover - interface
        """Opens at 1200 baud and closes: the bootloader entry request used by RP2040 boards."""
        raise NotImplementedError

    def close(self) -> None:  # pragma: no cover - interface
        raise NotImplementedError


class SerialOpener:
    def open(self, device: str, timeout_s: float) -> SerialPort:  # pragma: no cover - interface
        raise NotImplementedError


class PortEnumerator:
    def list_ports(self) -> List[PortInfo]:  # pragma: no cover - interface
        raise NotImplementedError


class VolumeFinder:
    def list_volumes(self) -> List[Volume]:  # pragma: no cover - interface
        raise NotImplementedError


class Clock:
    def monotonic(self) -> float:  # pragma: no cover - interface
        raise NotImplementedError

    def sleep(self, seconds: float) -> None:  # pragma: no cover - interface
        raise NotImplementedError


class Downloader:
    def fetch(self, url: str, max_bytes: int, token: Optional[str] = None,
              accept: str = "application/octet-stream") -> bytes:  # pragma: no cover - interface
        raise NotImplementedError


class Console:
    """User interaction. confirm_typed never consults assume_yes: destructive steps need text."""

    def say(self, message: str) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def warn(self, message: str) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def pause_for_action(self, message: str) -> None:  # pragma: no cover - interface
        """Waits for the user to perform a physical step, then press Enter."""
        raise NotImplementedError

    def confirm(self, message: str, default: bool = False) -> bool:  # pragma: no cover - interface
        """Non-destructive yes/no. Returns True when assume_yes is set."""
        raise NotImplementedError

    def confirm_typed(self, word: str, message: str) -> bool:  # pragma: no cover - interface
        raise NotImplementedError

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:  # pragma: no cover - interface
        raise NotImplementedError

    def ask(self, prompt: str) -> str:  # pragma: no cover - interface
        raise NotImplementedError
