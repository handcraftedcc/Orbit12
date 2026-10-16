"""UF2 block validation for the RP2040 image the installer is about to copy.

Every 512-byte block is checked: magic values, flags, family ID, payload bounds, the full
target range against the image bounds and against the filesystem partition, and block
numbering. Nothing is written here.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Optional

BLOCK_BYTES = 512
MAGIC_START0 = 0x0A324655  # "UF2\n"
MAGIC_START1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FLAG_NOT_MAIN_FLASH = 0x00000001
FLAG_FAMILY_ID_PRESENT = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
MAX_PAYLOAD = 476
MAX_BLOCKS = 1 << 16  # bounds the scan; the image size is limited by MAX_IMAGE_BYTES in package.py


class Uf2Error(Exception):
    """The image is not a valid RP2040 UF2 for this board, or it would touch the filesystem."""


@dataclass(frozen=True)
class Block:
    index: int
    flags: int
    target: int
    payload_size: int
    block_count: int
    family_id: Optional[int]


@dataclass(frozen=True)
class ImageSummary:
    blocks: int
    first_target: int
    end_target: int
    payload_bytes: int


def _ranges_overlap(start_a: int, end_a: int, start_b: int, end_b: int) -> bool:
    return start_a < end_b and start_b < end_a


def parse_block(raw: bytes, index: int) -> Block:
    if len(raw) != BLOCK_BYTES:
        raise Uf2Error(f"block {index}: not {BLOCK_BYTES} bytes")
    magic0, magic1, flags, target, payload_size, block_no, block_count, family = struct.unpack_from(
        "<8I", raw, 0)
    (magic_end,) = struct.unpack_from("<I", raw, BLOCK_BYTES - 4)
    if magic0 != MAGIC_START0 or magic1 != MAGIC_START1 or magic_end != MAGIC_END:
        raise Uf2Error(f"block {index}: bad UF2 magic")
    if flags & FLAG_NOT_MAIN_FLASH:
        raise Uf2Error(f"block {index}: NOT_MAIN_FLASH is set; this image is not for the flash")
    if not flags & FLAG_FAMILY_ID_PRESENT:
        raise Uf2Error(f"block {index}: no family ID; the RP2040 family is required")
    if payload_size == 0 or payload_size > MAX_PAYLOAD:
        raise Uf2Error(f"block {index}: payload size {payload_size} is out of range")
    if block_no != index:
        raise Uf2Error(f"block {index}: block number {block_no} is out of sequence")
    return Block(index, flags, target, payload_size, block_count, family)


def check_image(data: bytes, *, address_start: int, address_end: int, fs_start: int, fs_size: int,
                expected_family: int = RP2040_FAMILY_ID,
                expected_blocks: Optional[int] = None) -> ImageSummary:
    """Validates the whole image. Raises Uf2Error with the first problem found."""
    if not data or len(data) % BLOCK_BYTES:
        raise Uf2Error("image size is not a positive multiple of 512 bytes")
    count = len(data) // BLOCK_BYTES
    if count > MAX_BLOCKS:
        raise Uf2Error("image has too many blocks")
    if expected_blocks is not None and count != expected_blocks:
        raise Uf2Error(f"image has {count} blocks; the manifest says {expected_blocks}")
    fs_end = fs_start + fs_size
    if fs_size <= 0:
        raise Uf2Error("filesystem size must be positive")
    first_target = None
    end_target = 0
    payload_total = 0
    for index in range(count):
        block = parse_block(data[index * BLOCK_BYTES:(index + 1) * BLOCK_BYTES], index)
        if block.block_count != count:
            raise Uf2Error(f"block {index}: block count {block.block_count} does not match {count}")
        if block.family_id != expected_family:
            raise Uf2Error(
                f"block {index}: family ID 0x{block.family_id:08X} is not 0x{expected_family:08X}")
        block_end = block.target + block.payload_size
        if block.target < address_start or block_end > address_end:
            raise Uf2Error(
                f"block {index}: target 0x{block.target:08X}..0x{block_end:08X} is outside the "
                f"image range 0x{address_start:08X}..0x{address_end:08X}")
        if _ranges_overlap(block.target, block_end, fs_start, fs_end):
            raise Uf2Error(
                f"block {index}: target 0x{block.target:08X}..0x{block_end:08X} overlaps the "
                f"filesystem partition 0x{fs_start:08X}..0x{fs_end:08X}; refusing")
        first_target = block.target if first_target is None else min(first_target, block.target)
        end_target = max(end_target, block_end)
        payload_total += block.payload_size
    return ImageSummary(count, first_target or 0, end_target, payload_total)
