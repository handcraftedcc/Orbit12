#!/usr/bin/env python3
"""ELF symbol helpers for the Orbit12 firmware build.

Reads symbols with the pinned toolchain's arm-none-eabi-nm (never a PATH guess).

  python tools/arduino/elf_symbols.py check --nm NM --elf FILE.elf

`check` fails (exit 1) unless the four --wrap redirections are linked: each
__wrap_<symbol> must be defined in the ELF. The same module supplies the
filesystem bounds (_FS_start/_FS_end) to make_release_manifest.py.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass

# Keep in sync with WRAP_SYMBOLS in tools/arduino/build-firmware.sh and usb_core_guard.cpp.
WRAP_SYMBOLS = ("TinyUSB_Device_Task", "TinyUSB_Device_FlushCDC", "tud_task_ext", "tud_cdc_n_write_flush")
FS_START_SYMBOL = "_FS_start"
FS_END_SYMBOL = "_FS_end"

_NM_LINE = re.compile(r"^([0-9A-Fa-f]+)\s+(\S)\s+(\S+)\s*$")


class ElfError(RuntimeError):
    """Raised when the ELF cannot be read or a required symbol is missing."""


@dataclass(frozen=True)
class Symbol:
    address: int
    kind: str  # nm type letter; upper case is global, lower case local/weak variants
    name: str


def parse_nm_output(text: str) -> dict[str, Symbol]:
    """Parse `nm` output lines of the form 'ADDR TYPE NAME'. Undefined symbols (no address) are skipped."""
    symbols: dict[str, Symbol] = {}
    for line in text.splitlines():
        match = _NM_LINE.match(line)
        if not match:
            continue
        address, kind, name = int(match.group(1), 16), match.group(2), match.group(3)
        if kind in ("U", "w") and address == 0:
            continue
        symbols.setdefault(name, Symbol(address, kind, name))
    return symbols


def read_symbols(nm_path: str, elf_path: str) -> dict[str, Symbol]:
    try:
        result = subprocess.run(
            [nm_path, elf_path], check=False, capture_output=True, text=True, encoding="utf-8", errors="replace"
        )
    except OSError as exc:
        raise ElfError(f"cannot run nm '{nm_path}': {exc}") from exc
    if result.returncode != 0:
        raise ElfError(f"nm failed on {elf_path} (exit {result.returncode}): {result.stderr.strip()}")
    symbols = parse_nm_output(result.stdout)
    if not symbols:
        raise ElfError(f"nm produced no symbols for {elf_path}")
    return symbols


def check_wrap_symbols(symbols: dict[str, Symbol]) -> list[str]:
    """Return the names of __wrap_ symbols that are missing (empty list means all present)."""
    return [f"__wrap_{name}" for name in WRAP_SYMBOLS if f"__wrap_{name}" not in symbols]


def filesystem_bounds(symbols: dict[str, Symbol]) -> tuple[int, int]:
    """Return (start, end) of the LittleFS partition from the linker-provided symbols."""
    missing = [name for name in (FS_START_SYMBOL, FS_END_SYMBOL) if name not in symbols]
    if missing:
        raise ElfError(
            f"ELF lacks filesystem bound symbol(s) {', '.join(missing)}; the linker script must define them "
            "(arduino-pico memmap_default.ld PROVIDE _FS_start/_FS_end)"
        )
    start, end = symbols[FS_START_SYMBOL].address, symbols[FS_END_SYMBOL].address
    if end <= start:
        raise ElfError(f"filesystem bounds are empty or inverted: start=0x{start:08x} end=0x{end:08x}")
    return start, end


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check", help="verify the __wrap_ USB redirections are linked")
    check.add_argument("--nm", required=True, help="path to the pinned arm-none-eabi-nm")
    check.add_argument("--elf", required=True, help="path to the linked ELF")
    args = parser.parse_args(argv)
    try:
        symbols = read_symbols(args.nm, args.elf)
        missing = check_wrap_symbols(symbols)
        if missing:
            print(f"error: missing wrap symbols in {args.elf}: {', '.join(missing)}", file=sys.stderr)
            print("error: the --wrap linker flags were not applied; the USB guard is not linked", file=sys.stderr)
            return 1
        print(f"ELF wrap check passed: {len(WRAP_SYMBOLS)} __wrap_ symbols present in {args.elf}")
        return 0
    except ElfError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
