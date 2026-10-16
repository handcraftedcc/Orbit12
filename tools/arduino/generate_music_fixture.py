"""Regenerate checksums directly from the pure CircuitPython musical oracle."""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[2]
source = runpy.run_path(str(ROOT / "circuitpython-source/src/apps/Orion/core/music.py"))

def checksum(values):
    result = 2166136261
    for value in values:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result

notes = (0, 1, 11, 12, 59, 60, 61, 126, 127)
degrees = (-24, -1, 0, 1, 24)
octaves = (-2, 0, 2)
transpose = checksum(
    source["transpose"](note, degree, octave, True, root, scale)
    for scale in source["SCALES"] for root in range(12) for note in notes
    for degree in degrees for octave in octaves
)
tables = checksum(
    value for name in ("AUTOBORROWRELATIONSHIP", "RATE_MIDI_TICKS", "patterns_bit", "patterns_len")
    for value in source[name]
)
strings = checksum(
    value for name in ("NOTES", "SCALENAMES", "RATE_LABELS")
    for item in source[name] for value in item.encode("ascii") + b"\0"
)
target = ROOT / "tests/native/fixtures/t03_golden.hpp"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(
    "// Generated from core/music.py by tools/arduino/generate_music_fixture.py.\n"
    "#pragma once\n#include <cstdint>\nnamespace music_golden {\n"
    f"constexpr std::uint32_t transpose_checksum = 0x{transpose:08x}U;\n"
    f"constexpr std::uint32_t table_checksum = 0x{tables:08x}U;\n"
    f"constexpr std::uint32_t string_checksum = 0x{strings:08x}U;\n"
    "}\n", encoding="ascii", newline="\n")
print(target)
