#!/usr/bin/env python3
"""Create release-manifest.json and SHA256SUMS for an Orbit12 firmware image.

The installer consumes release-manifest.json. Field names and layout are a fixed schema
(schema 1); do not rename fields without bumping "schema" and the installer together.

  python tools/arduino/make_release_manifest.py \
      --uf2 .tool-cache/artifacts/release/dist/Orbit12-0.1.0-pre.1.uf2 \
      --elf .tool-cache/artifacts/release/Orbit12.ino.elf \
      --nm  <pinned arm-none-eabi-nm> \
      --version 0.1.0-pre.1 --git-sha <sha> --out-dir <dir>

Checks (any failure exits 1 with no output written):
  * UF2 blocks are well formed, carry the RP2040 family id, and are numbered in order.
  * The image lies in flash at or above 0x10000000 and does not overlap the LittleFS partition.
  * The filesystem bounds come from the ELF symbols _FS_start/_FS_end (never guessed).
  * The ELF contains the release version and git sha strings, so an unstamped build is refused.
  * Module ids come from modules/generated/module_inventory.json and match the module.json manifests
    of the sketch (the release set: core plus enabled user modules).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elf_symbols  # noqa: E402  (sibling module; path inserted above)

SCHEMA = 1
PRODUCT = "orbit12"
CHIP = "RP2040"
UF2_FAMILY_RP2040 = 0xE48BFF56
FLASH_BASE = 0x10000000
UF2_BLOCK_SIZE = 512
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
UF2_MAX_PAYLOAD = 476
DEFAULT_REPO = "handcraftedcc/Orbit12"

SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$"
)
GIT_SHA = re.compile(r"^[0-9a-f]{7,40}$")


class ManifestError(RuntimeError):
    """A release input is missing, malformed or inconsistent."""


@dataclass(frozen=True)
class Uf2Summary:
    blocks: int
    start: int
    end: int  # exclusive


def parse_uf2(data: bytes) -> Uf2Summary:
    if len(data) == 0 or len(data) % UF2_BLOCK_SIZE != 0:
        raise ManifestError(f"UF2 size {len(data)} is not a positive multiple of {UF2_BLOCK_SIZE}")
    block_count = len(data) // UF2_BLOCK_SIZE
    start = None
    end = 0
    for index in range(block_count):
        block = data[index * UF2_BLOCK_SIZE:(index + 1) * UF2_BLOCK_SIZE]
        magic0, magic1, flags, target, payload, block_no, num_blocks, family = struct.unpack_from("<8I", block, 0)
        (magic_end,) = struct.unpack_from("<I", block, UF2_BLOCK_SIZE - 4)
        if (magic0, magic1, magic_end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise ManifestError(f"UF2 block {index} has bad magic")
        if block_no != index or num_blocks != block_count:
            raise ManifestError(f"UF2 block {index} is numbered {block_no}/{num_blocks}, expected {index}/{block_count}")
        if payload == 0 or payload > UF2_MAX_PAYLOAD:
            raise ManifestError(f"UF2 block {index} has payload size {payload}")
        if not flags & UF2_FLAG_FAMILY_ID:
            raise ManifestError(f"UF2 block {index} does not declare a family id")
        if family != UF2_FAMILY_RP2040:
            raise ManifestError(f"UF2 block {index} family 0x{family:08x} is not RP2040 0x{UF2_FAMILY_RP2040:08x}")
        if target < FLASH_BASE:
            raise ManifestError(f"UF2 block {index} targets 0x{target:08x}, below flash base")
        start = target if start is None else min(start, target)
        end = max(end, target + payload)
    assert start is not None
    return Uf2Summary(blocks=block_count, start=start, end=end)


INVENTORY_RELATIVE = os.path.join("src", "modules", "generated", "module_inventory.json")


def parse_inventory(inventory_text: str) -> tuple[list[str], list[str]]:
    """Core and user module ids from module_inventory.json (written by tools/modules/gen_registry.py)."""
    try:
        data = json.loads(inventory_text)
    except ValueError as exc:
        raise ManifestError(f"module inventory is not JSON: {exc}") from exc
    if not isinstance(data, dict) or not isinstance(data.get("core"), list) or not isinstance(data.get("user"), list):
        raise ManifestError("module inventory lacks the core or user list")
    core = [str(item) for item in data["core"]]
    user = [str(item) for item in data["user"]]
    expected = {"core_count": len(core), "user_count": len(user), "total_count": len(core) + len(user)}
    for key, count in expected.items():
        if data.get(key) != count:
            raise ManifestError(f"module inventory {key} is {data.get(key)!r}, but its lists hold {count}")
    ids = core + user
    if len(set(ids)) != len(ids):
        raise ManifestError("duplicate module ids in the module inventory")
    if not core:
        raise ManifestError("module inventory lists no core modules")
    return core, user


def _manifest_id(path: str) -> tuple[str, bool]:
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return str(data["module_id"]), bool(data.get("enabled_in_release", True))


def check_inventory_against_manifests(sketch_dir: str, core: list[str], user: list[str]) -> None:
    """The inventory must list the module.json files of the tree that was compiled (cross-check)."""
    core_root = os.path.join(sketch_dir, "src", "modules", "core")
    found_core = []
    for name in sorted(os.listdir(core_root)):
        path = os.path.join(core_root, name, "module.json")
        if os.path.isfile(path):
            found_core.append(_manifest_id(path)[0])
    found_user = []
    user_root = os.path.join(sketch_dir, "src", "modules", "user")
    if os.path.isdir(user_root):
        for author in sorted(os.listdir(user_root)):
            author_dir = os.path.join(user_root, author)
            for name in sorted(os.listdir(author_dir)) if os.path.isdir(author_dir) else []:
                path = os.path.join(author_dir, name, "module.json")
                if os.path.isfile(path):
                    module_id, enabled = _manifest_id(path)
                    if enabled:
                        found_user.append(module_id)
    if sorted(found_core) != sorted(core):
        raise ManifestError(f"module inventory core ids {sorted(core)} differ from the core manifests {sorted(found_core)}")
    if sorted(found_user) != sorted(user):
        raise ManifestError(f"module inventory user ids {sorted(user)} differ from the enabled user manifests {sorted(found_user)}")


def read_build_identity(build_info_text: str) -> tuple[int, str]:
    api = re.search(r"kEngineApi\s*=\s*(\d+)", build_info_text)
    board = re.search(r'kBoardId\[\]\s*=\s*"([^"]+)"', build_info_text)
    if api is None or board is None:
        raise ManifestError("build_info.hpp lacks kEngineApi or kBoardId")
    return int(api.group(1)), board.group(1)


def build_manifest(
    *,
    user_ids: list[str] | None = None,
    uf2_name: str,
    uf2_data: bytes,
    symbols: dict[str, elf_symbols.Symbol],
    elf_data: bytes | None,
    version: str,
    git_sha: str,
    core_ids: list[str],
    build_info_text: str,
    repo: str = DEFAULT_REPO,
    allow_dirty: bool = False,
) -> dict:
    if not SEMVER.match(version):
        raise ManifestError(f"version '{version}' is not semver (for example 0.1.0-pre.1)")
    base_sha = git_sha[: -len("-dirty")] if git_sha.endswith("-dirty") else git_sha
    if not GIT_SHA.match(base_sha):
        raise ManifestError(f"git sha '{git_sha}' is not a lowercase hex commit id")
    if git_sha != base_sha and not allow_dirty:
        raise ManifestError("git sha is marked dirty; release manifests need a clean tree (--allow-dirty for tests)")
    expected_uf2 = f"Orbit12-{version}.uf2"
    if uf2_name != expected_uf2:
        raise ManifestError(f"UF2 file must be named {expected_uf2}, got {uf2_name}")

    image = parse_uf2(uf2_data)
    fs_start, fs_end = elf_symbols.filesystem_bounds(symbols)
    if image.end > fs_start and image.start < fs_end:
        raise ManifestError(
            f"image 0x{image.start:08x}-0x{image.end:08x} overlaps filesystem "
            f"0x{fs_start:08x}-0x{fs_end:08x}"
        )

    if elf_data is not None:
        for needle, label in ((version, "version"), (base_sha, "git sha")):
            if needle.encode("ascii") not in elf_data:
                raise ManifestError(f"ELF lacks the {label} string '{needle}'; the build was not stamped")

    engine_api, board = read_build_identity(build_info_text)
    user_ids = list(user_ids or [])
    return {
        "schema": SCHEMA,
        "product": PRODUCT,
        "version": version,
        "prerelease": "-" in version,
        "git_sha": git_sha,
        "engine_api": engine_api,
        "board": board,
        "chip": CHIP,
        "uf2_family_id": f"0x{UF2_FAMILY_RP2040:08x}",
        "image": {
            "file": uf2_name,
            "sha256": hashlib.sha256(uf2_data).hexdigest(),
            "size": len(uf2_data),
            "address_start": f"0x{image.start:08x}",
            "address_end": f"0x{image.end:08x}",
            "blocks": image.blocks,
        },
        "filesystem": {"start": f"0x{fs_start:08x}", "size": fs_end - fs_start},
        "modules": {"core": core_ids, "user": user_ids},
        "repo": repo,
    }


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


DEFAULT_SKETCH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../firmware/Orbit12"))


def build_parser(default_sketch: str = DEFAULT_SKETCH) -> argparse.ArgumentParser:
    """The command line. build-firmware.sh invokes this tool; tests check that its flags match this parser."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--uf2", required=True, help="release UF2 (named Orbit12-<version>.uf2)")
    parser.add_argument("--elf", required=True, help="linked ELF from the same build")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--nm", help="pinned arm-none-eabi-nm used to read the ELF symbols")
    source.add_argument("--nm-output", help="saved nm output (offline/test use)")
    parser.add_argument("--version", required=True, help="semver release version, e.g. 0.1.0-pre.1")
    parser.add_argument("--git-sha", required=True, help="commit id stamped into the firmware")
    parser.add_argument("--out-dir", required=True, help="directory for release-manifest.json and SHA256SUMS")
    parser.add_argument("--repo", default=DEFAULT_REPO, help="GitHub repo that hosts the release")
    parser.add_argument("--sketch-dir", default=default_sketch, help="firmware/Orbit12 folder")
    parser.add_argument("--allow-dirty", action="store_true", help="accept a -dirty git sha (local checks only)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        with open(args.uf2, "rb") as handle:
            uf2_data = handle.read()
        with open(args.elf, "rb") as handle:
            elf_data = handle.read()
        if args.nm is not None:
            symbols = elf_symbols.read_symbols(args.nm, args.elf)
        else:
            with open(args.nm_output, encoding="utf-8", errors="replace") as handle:
                symbols = elf_symbols.parse_nm_output(handle.read())
        # Module ids: the inventory written by tools/modules/gen_registry.py, cross-checked against the manifests.
        with open(os.path.join(args.sketch_dir, INVENTORY_RELATIVE), encoding="utf-8") as handle:
            core_ids, user_ids = parse_inventory(handle.read())
        check_inventory_against_manifests(args.sketch_dir, core_ids, user_ids)
        build_info_path = os.path.join(args.sketch_dir, "src", "app", "build_info.hpp")
        with open(build_info_path, encoding="utf-8") as handle:
            build_info_text = handle.read()
        manifest = build_manifest(
            uf2_name=os.path.basename(args.uf2),
            uf2_data=uf2_data,
            symbols=symbols,
            elf_data=elf_data,
            version=args.version,
            git_sha=args.git_sha,
            core_ids=core_ids,
            user_ids=user_ids,
            build_info_text=build_info_text,
            repo=args.repo,
            allow_dirty=args.allow_dirty,
        )
    except (OSError, ValueError, elf_symbols.ElfError, ManifestError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    os.makedirs(args.out_dir, exist_ok=True)
    manifest_path = os.path.join(args.out_dir, "release-manifest.json")
    text = json.dumps(manifest, indent=2) + "\n"
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    sums_path = os.path.join(args.out_dir, "SHA256SUMS")
    with open(sums_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"{sha256_file(args.uf2)}  {os.path.basename(args.uf2)}\n")
        handle.write(f"{sha256_file(manifest_path)}  release-manifest.json\n")
    print(text, end="")
    print(f"wrote {manifest_path}", file=sys.stderr)
    print(f"wrote {sums_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
