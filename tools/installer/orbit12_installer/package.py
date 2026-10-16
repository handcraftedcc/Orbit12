"""Release manifest, SHA256SUMS and local package verification.

Pure functions only: nothing here downloads or writes. The manifest schema is fixed by
Package A (schema 1); field names must not change.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from typing import Dict, Mapping, Optional

from . import uf2

SCHEMA = 1
PRODUCT = "orbit12"
BOARD = "adafruit_macropad_rp2040"
CHIP = "RP2040"
FAMILY_ID = "0xe48bff56"
# The RP2040 maps QSPI flash into this window (XIP). Flash size is not assumed: the image and the
# filesystem partition are checked against the window, not against a fixed flash size.
XIP_BASE = 0x10000000
XIP_LIMIT = 0x11000000  # end of the 16 MiB window; exclusive
FS_ALIGN = 4096
MANIFEST_NAME = "release-manifest.json"
SUMS_NAME = "SHA256SUMS"
MAX_MANIFEST_BYTES = 64 * 1024
MAX_SUMS_BYTES = 16 * 1024
MAX_IMAGE_BYTES = 8 * 1024 * 1024

_SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$")
_HEX_SHA = re.compile(r"^[0-9a-f]{64}$")
_HEX_INT = re.compile(r"^0x[0-9a-fA-F]{1,8}$")
_VERSION = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+-]{0,63}$")
_IMAGE_FILE = re.compile(r"^Orbit12-[0-9A-Za-z][0-9A-Za-z.+-]{0,63}\.uf2$")


class PackageError(Exception):
    """A manifest, checksum list or image failed verification. The message is user-facing."""


def safe_name(name: str) -> str:
    """Accepts a bare file name only: no separators, no drive letters, no dot-dot."""
    if not isinstance(name, str) or not _SAFE_NAME.match(name) or ".." in name:
        raise PackageError(f"unsafe file name in release data: {name!r}")
    return name


@dataclass(frozen=True)
class Image:
    file: str
    sha256: str
    size: int
    address_start: int
    address_end: int
    blocks: int


@dataclass(frozen=True)
class Manifest:
    version: str
    prerelease: bool
    git_sha: str
    engine_api: int
    repo: str
    image: Image
    fs_start: int
    fs_size: int
    core_modules: tuple
    user_modules: tuple


def _hex_int(value, label: str) -> int:
    if not isinstance(value, str) or not _HEX_INT.match(value):
        raise PackageError(f"manifest {label} must be a 0x hex string")
    return int(value, 16)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise PackageError(message)


def parse_manifest(data: bytes, *, expected_repo: Optional[str] = None) -> Manifest:
    if len(data) > MAX_MANIFEST_BYTES:
        raise PackageError("release manifest is larger than the installer accepts")
    try:
        doc = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        raise PackageError(f"release manifest is not valid JSON: {error}") from None
    _require(isinstance(doc, dict), "release manifest must be a JSON object")
    _require(doc.get("schema") == SCHEMA, f"manifest schema {doc.get('schema')!r} is not {SCHEMA}")
    _require(doc.get("product") == PRODUCT, f"manifest product {doc.get('product')!r} is not orbit12")
    _require(doc.get("board") == BOARD,
             f"manifest is for board {doc.get('board')!r}; this installer flashes {BOARD}")
    _require(doc.get("chip") == CHIP, f"manifest chip {doc.get('chip')!r} is not {CHIP}")
    _require(str(doc.get("uf2_family_id", "")).lower() == FAMILY_ID,
             "manifest UF2 family ID is not the RP2040 family")
    version = doc.get("version")
    _require(isinstance(version, str) and _VERSION.match(version) is not None,
             "manifest version is missing or malformed")
    _require(isinstance(doc.get("prerelease"), bool), "manifest prerelease must be true or false")
    _require(isinstance(doc.get("engine_api"), int) and not isinstance(doc.get("engine_api"), bool),
             "manifest engine_api must be an integer")
    git_sha = doc.get("git_sha")
    _require(isinstance(git_sha, str) and re.match(r"^[0-9a-f]{7,40}$", git_sha) is not None,
             "manifest git_sha is missing or malformed")
    repo = doc.get("repo")
    _require(isinstance(repo, str) and re.match(r"^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$", repo) is not None,
             "manifest repo is missing or malformed")
    if expected_repo is not None:
        _require(repo.lower() == expected_repo.lower(),
                 f"manifest repo {repo!r} does not match the requested repo {expected_repo!r}")

    image = doc.get("image")
    _require(isinstance(image, dict), "manifest image section is missing")
    file_name = image.get("file")
    _require(isinstance(file_name, str) and _IMAGE_FILE.match(file_name) is not None,
             "manifest image file name is not Orbit12-<version>.uf2")
    _require(file_name == f"Orbit12-{version}.uf2",
             "manifest image file name does not match its version")
    sha = image.get("sha256")
    _require(isinstance(sha, str) and _HEX_SHA.match(sha) is not None,
             "manifest image sha256 is malformed")
    size = image.get("size")
    _require(isinstance(size, int) and 0 < size <= MAX_IMAGE_BYTES and size % uf2.BLOCK_BYTES == 0,
             "manifest image size is out of range")
    blocks = image.get("blocks")
    _require(isinstance(blocks, int) and blocks == size // uf2.BLOCK_BYTES,
             "manifest image block count does not match its size")
    start = _hex_int(image.get("address_start"), "image.address_start")
    end = _hex_int(image.get("address_end"), "image.address_end")

    fs = doc.get("filesystem")
    _require(isinstance(fs, dict), "manifest filesystem section is missing")
    fs_start = _hex_int(fs.get("start"), "filesystem.start")
    fs_size = fs.get("size")
    _require(isinstance(fs_size, int) and not isinstance(fs_size, bool), "manifest filesystem size is invalid")
    check_layout(start, end, fs_start, fs_size)

    modules = doc.get("modules")
    _require(isinstance(modules, dict), "manifest modules section is missing")
    core = modules.get("core", [])
    user = modules.get("user", [])
    _require(isinstance(core, list) and isinstance(user, list), "manifest module lists are malformed")
    return Manifest(
        version=version,
        prerelease=doc["prerelease"],
        git_sha=git_sha,
        engine_api=doc["engine_api"],
        repo=repo,
        image=Image(file_name, sha, size, start, end, blocks),
        fs_start=fs_start,
        fs_size=fs_size,
        core_modules=tuple(core),
        user_modules=tuple(user),
    )


def check_layout(image_start: int, image_end: int, fs_start: int, fs_size: int) -> None:
    """The image window and the filesystem window must be sane and must not overlap.

    No flash size is assumed: the image window and the filesystem window only have to lie inside
    the 16 MiB XIP window, with the image below the filesystem. The real clean build has the image
    at 0x10000000..0x10043000 and the filesystem at 0x106ff000 with a 1 MiB size.
    """
    _require(XIP_BASE <= image_start < image_end <= XIP_LIMIT,
             f"manifest image window 0x{image_start:08X}..0x{image_end:08X} is outside the RP2040 "
             f"flash window 0x{XIP_BASE:08X}..0x{XIP_LIMIT:08X}")
    _require(fs_size > 0, "manifest filesystem size must be positive")
    _require(fs_size % FS_ALIGN == 0,
             f"manifest filesystem size {fs_size} is not a multiple of {FS_ALIGN} bytes")
    _require(fs_start % FS_ALIGN == 0,
             f"manifest filesystem start 0x{fs_start:08X} is not aligned to {FS_ALIGN} bytes")
    _require(XIP_BASE <= fs_start and fs_start + fs_size <= XIP_LIMIT,
             f"manifest filesystem window 0x{fs_start:08X}..0x{fs_start + fs_size:08X} is outside the "
             f"RP2040 flash window 0x{XIP_BASE:08X}..0x{XIP_LIMIT:08X}")
    _require(image_end <= fs_start,
             f"manifest image window ends at 0x{image_end:08X}, past the filesystem start "
             f"0x{fs_start:08X}: the image would overlap the filesystem")


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_sha256sums(data: bytes) -> Dict[str, str]:
    """'<hex>  <name>' lines (text or binary mode marker '*'). Duplicates are refused."""
    if len(data) > MAX_SUMS_BYTES:
        raise PackageError("SHA256SUMS is larger than the installer accepts")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise PackageError("SHA256SUMS is not UTF-8 text") from None
    entries: Dict[str, str] = {}
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        parts = re.match(r"^([0-9a-fA-F]{64}) [ *](.+)$", line.rstrip("\r"))
        if parts is None:
            raise PackageError(f"SHA256SUMS line {number} is malformed")
        digest, name = parts.group(1).lower(), parts.group(2)
        safe_name(name)
        if name in entries:
            raise PackageError(f"SHA256SUMS lists {name} twice")
        entries[name] = digest
    return entries


def verify_image_bytes(image_bytes: bytes, manifest: Manifest,
                       sums: Optional[Mapping[str, str]] = None) -> None:
    """Checks size, hash (manifest and SHA256SUMS) and the UF2 structure. Raises PackageError."""
    if len(image_bytes) != manifest.image.size:
        raise PackageError(
            f"image is {len(image_bytes)} bytes; the manifest says {manifest.image.size}")
    digest = sha256_hex(image_bytes)
    if digest != manifest.image.sha256:
        raise PackageError("image SHA-256 does not match release-manifest.json; refusing it")
    if sums is not None:
        listed = sums.get(manifest.image.file)
        if listed is None:
            raise PackageError(f"SHA256SUMS has no entry for {manifest.image.file}")
        if listed != digest:
            raise PackageError(
                "SHA256SUMS disagrees with the downloaded image; the release data is inconsistent "
                "or was altered. Refusing it.")
        if listed != manifest.image.sha256:
            raise PackageError("SHA256SUMS and release-manifest.json disagree on the image hash")
    try:
        uf2.check_image(
            image_bytes,
            address_start=manifest.image.address_start,
            address_end=manifest.image.address_end,
            fs_start=manifest.fs_start,
            fs_size=manifest.fs_size,
            expected_blocks=manifest.image.blocks,
        )
    except uf2.Uf2Error as error:
        raise PackageError(f"UF2 check failed: {error}") from None


def tag_matches_version(tag: str, version: str) -> bool:
    return tag[1:] == version if tag.startswith("v") else tag == version
