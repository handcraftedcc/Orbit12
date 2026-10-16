"""Tests for make_release_manifest.py and elf_symbols.py using a synthetic UF2 and fake nm output.

Run from the repository root:
  python -m unittest discover -s tools/arduino/tests -v
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import re
import struct
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, TOOLS_DIR)

import elf_symbols  # noqa: E402
import make_release_manifest as mrm  # noqa: E402

VERSION = "0.1.0-pre.1"
SHA = "0123456789abcdef0123456789abcdef01234567"
FS_START = 0x106FF000
FS_END = 0x107FF000
WRAP_NM = "\n".join(f"20000000 T __wrap_{name}" for name in elf_symbols.WRAP_SYMBOLS)
FAKE_NM = "\n".join(
    [
        "10000000 T __flash_binary_start",
        f"{FS_START:08x} A _FS_start",
        f"{FS_END:08x} A _FS_end",
        "         U unresolved_symbol",
        WRAP_NM,
    ]
)
CORE_IDS = ["note", "arp", "wander"]  # stands for the generated module inventory
BUILD_INFO = """inline constexpr const char kFirmwareVersion[] = "0.1.0-pre.1";
inline constexpr const char kGitSha[] = "0123456789abcdef0123456789abcdef01234567";
inline constexpr std::uint32_t kEngineApi = 1;
inline constexpr const char kBoardId[] = "adafruit_macropad_rp2040";
"""
ELF_WITH_STAMP = (b"\x7fELF" + VERSION.encode() + b"\x00" + SHA.encode() + b"\x00")


def make_block(target: int, payload: bytes, block_no: int, num_blocks: int,
               family: int = mrm.UF2_FAMILY_RP2040, flags: int = mrm.UF2_FLAG_FAMILY_ID) -> bytes:
    block = bytearray(512)
    struct.pack_into("<8I", block, 0, mrm.UF2_MAGIC_START0, mrm.UF2_MAGIC_START1, flags, target,
                     len(payload), block_no, num_blocks, family)
    block[32:32 + len(payload)] = payload
    struct.pack_into("<I", block, 508, mrm.UF2_MAGIC_END)
    return bytes(block)


def make_uf2(payload_blocks: int = 3, base: int = 0x10000000) -> bytes:
    return b"".join(
        make_block(base + index * 256, bytes([index + 1]) * 256, index, payload_blocks)
        for index in range(payload_blocks)
    )


def symbols_from(text: str) -> dict:
    return elf_symbols.parse_nm_output(text)


def build(uf2: bytes | None = None, **kwargs) -> dict:
    options = dict(
        uf2_name=f"Orbit12-{VERSION}.uf2",
        uf2_data=uf2 if uf2 is not None else make_uf2(),
        symbols=symbols_from(FAKE_NM),
        elf_data=ELF_WITH_STAMP,
        version=VERSION,
        git_sha=SHA,
        core_ids=CORE_IDS,
        build_info_text=BUILD_INFO,
    )
    options.update(kwargs)
    return mrm.build_manifest(**options)


class ManifestHappyPath(unittest.TestCase):
    def test_schema_fields_and_values(self) -> None:
        uf2 = make_uf2()
        manifest = build(uf2)
        self.assertEqual(list(manifest), ["schema", "product", "version", "prerelease", "git_sha", "engine_api",
                                          "board", "chip", "uf2_family_id", "image", "filesystem", "modules",
                                          "repo"])
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["product"], "orbit12")
        self.assertTrue(manifest["prerelease"])
        self.assertEqual(manifest["engine_api"], 1)
        self.assertEqual(manifest["board"], "adafruit_macropad_rp2040")
        self.assertEqual(manifest["chip"], "RP2040")
        self.assertEqual(manifest["uf2_family_id"], "0xe48bff56")
        self.assertEqual(manifest["repo"], "handcraftedcc/Orbit12")
        image = manifest["image"]
        self.assertEqual(image["file"], f"Orbit12-{VERSION}.uf2")
        self.assertEqual(image["sha256"], hashlib.sha256(uf2).hexdigest())
        self.assertEqual(image["size"], len(uf2))
        self.assertEqual(image["address_start"], "0x10000000")
        self.assertEqual(image["address_end"], "0x10000300")
        self.assertEqual(image["blocks"], 3)
        self.assertEqual(manifest["filesystem"], {"start": "0x106ff000", "size": 1048576})
        self.assertEqual(manifest["modules"], {"core": ["note", "arp", "wander"], "user": []})

    def test_release_version_is_not_prerelease(self) -> None:
        manifest = build(make_uf2(), version="0.1.0", uf2_name="Orbit12-0.1.0.uf2",
                         elf_data=b"0.1.0" + SHA.encode())
        self.assertFalse(manifest["prerelease"])


class ManifestRejections(unittest.TestCase):
    def test_bad_version_rejected(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "semver"):
            build(version="v1", uf2_name="Orbit12-v1.uf2")

    def test_uf2_name_must_match_version(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "must be named"):
            build(uf2_name="firmware.uf2")

    def test_dirty_sha_refused_without_flag(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "dirty"):
            build(git_sha=SHA + "-dirty")

    def test_image_overlapping_filesystem_rejected(self) -> None:
        uf2 = make_uf2(payload_blocks=3, base=FS_START - 256)  # last block lands in the filesystem
        with self.assertRaisesRegex(mrm.ManifestError, "overlaps filesystem"):
            build(uf2)

    def test_missing_filesystem_symbols_fail_loudly(self) -> None:
        symbols = symbols_from(WRAP_NM)
        with self.assertRaisesRegex(elf_symbols.ElfError, "_FS_start"):
            build(symbols=symbols)

    def test_bad_uf2_magic_rejected(self) -> None:
        uf2 = bytearray(make_uf2())
        uf2[0:4] = b"XXXX"
        with self.assertRaisesRegex(mrm.ManifestError, "bad magic"):
            build(bytes(uf2))

    def test_wrong_family_rejected(self) -> None:
        uf2 = make_block(0x10000000, b"a" * 256, 0, 1, family=0x12345678)
        with self.assertRaisesRegex(mrm.ManifestError, "not RP2040"):
            build(uf2)

    def test_truncated_uf2_rejected(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "multiple"):
            build(make_uf2()[:-10])

    def test_block_numbering_checked(self) -> None:
        uf2 = make_block(0x10000000, b"a" * 256, 0, 2) + make_block(0x10000100, b"b" * 256, 5, 2)
        with self.assertRaisesRegex(mrm.ManifestError, "numbered"):
            build(uf2)

    def test_unstamped_elf_rejected(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "not stamped"):
            build(elf_data=b"placeholder only")



class ElfSymbols(unittest.TestCase):
    def test_wrap_symbols_reported_missing(self) -> None:
        present = symbols_from(WRAP_NM)
        self.assertEqual(elf_symbols.check_wrap_symbols(present), [])
        partial = symbols_from("\n".join(f"20000000 T __wrap_{n}" for n in elf_symbols.WRAP_SYMBOLS[:2]))
        self.assertEqual(elf_symbols.check_wrap_symbols(partial),
                         [f"__wrap_{n}" for n in elf_symbols.WRAP_SYMBOLS[2:]])

    def test_undefined_symbols_are_ignored(self) -> None:
        parsed = symbols_from("         U tud_task_ext\n00000000 w weak_undefined\n")
        self.assertEqual(parsed, {})

    def test_filesystem_bounds_read_from_symbols(self) -> None:
        self.assertEqual(elf_symbols.filesystem_bounds(symbols_from(FAKE_NM)), (FS_START, FS_END))


class CommandLine(unittest.TestCase):
    def test_writes_manifest_and_sums_then_refuses_bad_input(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sketch = os.path.join(tmp, "sketch")
            os.makedirs(os.path.join(sketch, "src", "app"))
            os.makedirs(os.path.join(sketch, "src", "modules", "generated"))
            with open(os.path.join(sketch, "src", "modules", "generated", "module_inventory.json"), "w", encoding="utf-8") as fh:
                json.dump({"core": CORE_IDS, "user": [], "core_count": 3, "user_count": 0, "total_count": 3}, fh)
            for module_id in CORE_IDS:
                os.makedirs(os.path.join(sketch, "src", "modules", "core", module_id), exist_ok=True)
                with open(os.path.join(sketch, "src", "modules", "core", module_id, "module.json"), "w", encoding="utf-8") as fh:
                    json.dump({"module_id": module_id}, fh)
            with open(os.path.join(sketch, "src", "app", "build_info.hpp"), "w", encoding="utf-8") as fh:
                fh.write(BUILD_INFO)
            uf2_path = os.path.join(tmp, f"Orbit12-{VERSION}.uf2")
            with open(uf2_path, "wb") as fh:
                fh.write(make_uf2())
            elf_path = os.path.join(tmp, "Orbit12.ino.elf")
            with open(elf_path, "wb") as fh:
                fh.write(ELF_WITH_STAMP)
            nm_path = os.path.join(tmp, "nm.txt")
            with open(nm_path, "w", encoding="utf-8") as fh:
                fh.write(FAKE_NM)
            out = os.path.join(tmp, "out")
            argv = ["--uf2", uf2_path, "--elf", elf_path, "--nm-output", nm_path, "--version", VERSION,
                    "--git-sha", SHA, "--out-dir", out, "--sketch-dir", sketch]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(mrm.main(argv), 0)
            with open(os.path.join(out, "release-manifest.json"), encoding="utf-8") as fh:
                self.assertEqual(json.load(fh)["filesystem"]["start"], "0x106ff000")
            with open(os.path.join(out, "SHA256SUMS"), encoding="utf-8") as fh:
                lines = fh.read().splitlines()
            self.assertEqual(len(lines), 2)
            self.assertTrue(lines[0].endswith(f"  Orbit12-{VERSION}.uf2"))

            bad_out = os.path.join(tmp, "bad")
            with open(nm_path, "w", encoding="utf-8") as fh:
                fh.write(WRAP_NM)  # no filesystem symbols
            bad_argv = argv[:argv.index("--out-dir")] + ["--out-dir", bad_out, "--sketch-dir", sketch]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(mrm.main(bad_argv), 1)
            self.assertFalse(os.path.exists(os.path.join(bad_out, "release-manifest.json")))


class InventoryParsing(unittest.TestCase):
    def test_parses_core_and_user_lists(self) -> None:
        text = json.dumps({"core": ["note", "arp"], "user": ["user.a.b"], "core_count": 2,
                           "user_count": 1, "total_count": 3})
        self.assertEqual(mrm.parse_inventory(text), (["note", "arp"], ["user.a.b"]))

    def test_count_mismatch_rejected(self) -> None:
        text = json.dumps({"core": ["note", "arp"], "user": [], "core_count": 3, "user_count": 0, "total_count": 3})
        with self.assertRaisesRegex(mrm.ManifestError, "core_count"):
            mrm.parse_inventory(text)

    def test_duplicate_ids_rejected(self) -> None:
        text = json.dumps({"core": ["note", "note"], "user": [], "core_count": 2, "user_count": 0, "total_count": 2})
        with self.assertRaisesRegex(mrm.ManifestError, "duplicate"):
            mrm.parse_inventory(text)

    def test_missing_lists_rejected(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "lacks"):
            mrm.parse_inventory("{}")

    def test_not_json_rejected(self) -> None:
        with self.assertRaisesRegex(mrm.ManifestError, "not JSON"):
            mrm.parse_inventory("core = []")


class InventoryAgainstManifests(unittest.TestCase):
    def write_sketch(self, root: str, core: list[str], user: list[tuple[str, str, bool]] = ()) -> str:
        for module_id in core:
            path = os.path.join(root, "src", "modules", "core", module_id)
            os.makedirs(path, exist_ok=True)
            with open(os.path.join(path, "module.json"), "w", encoding="utf-8") as fh:
                json.dump({"module_id": module_id}, fh)
        for author, name, enabled in user:
            path = os.path.join(root, "src", "modules", "user", author, name)
            os.makedirs(path, exist_ok=True)
            with open(os.path.join(path, "module.json"), "w", encoding="utf-8") as fh:
                json.dump({"module_id": f"user.{author}.{name}", "enabled_in_release": enabled}, fh)
        return root

    def test_matching_manifests_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.write_sketch(tmp, ["note", "arp"], [("a", "b", True), ("a", "off", False)])
            mrm.check_inventory_against_manifests(sketch, ["note", "arp"], ["user.a.b"])

    def test_missing_core_manifest_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.write_sketch(tmp, ["note"])
            with self.assertRaisesRegex(mrm.ManifestError, "core ids"):
                mrm.check_inventory_against_manifests(sketch, ["note", "arp"], [])

    def test_disabled_user_module_is_not_in_the_release(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.write_sketch(tmp, ["note"], [("a", "off", False)])
            with self.assertRaisesRegex(mrm.ManifestError, "user ids"):
                mrm.check_inventory_against_manifests(sketch, ["note"], ["user.a.off"])


REAL_SKETCH = os.path.abspath(os.path.join(TOOLS_DIR, "..", "..", "firmware", "Orbit12"))


class RealRepositoryEndToEnd(unittest.TestCase):
    """Runs the manifest tool on the real firmware sketch: the inventory and manifests are the committed
    generated files. Only the UF2, the ELF and the nm symbols are synthetic (the ELF carries the stamp)."""

    def test_manifest_from_the_real_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            uf2_path = os.path.join(tmp, f"Orbit12-{VERSION}.uf2")
            with open(uf2_path, "wb") as fh:
                fh.write(make_uf2())
            elf_path = os.path.join(tmp, "Orbit12.ino.elf")
            with open(elf_path, "wb") as fh:
                fh.write(ELF_WITH_STAMP)
            nm_path = os.path.join(tmp, "nm.txt")
            with open(nm_path, "w", encoding="utf-8") as fh:
                fh.write(FAKE_NM)
            out = os.path.join(tmp, "out")
            argv = ["--uf2", uf2_path, "--elf", elf_path, "--nm-output", nm_path, "--version", VERSION,
                    "--git-sha", SHA, "--out-dir", out, "--sketch-dir", REAL_SKETCH]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(mrm.main(argv), 0)
            with open(os.path.join(out, "release-manifest.json"), encoding="utf-8") as fh:
                manifest = json.load(fh)
            with open(os.path.join(out, "SHA256SUMS"), encoding="utf-8") as fh:
                sums = dict(reversed(line.split()) for line in fh.read().splitlines())
            with open(os.path.join(REAL_SKETCH, "src", "modules", "generated", "module_inventory.json"), encoding="utf-8") as fh:
                inventory = json.load(fh)
        self.assertEqual(len(manifest["modules"]["core"]), 24)
        self.assertEqual(manifest["modules"]["core"], inventory["core"])
        self.assertEqual(manifest["modules"]["user"], inventory["user"])
        self.assertEqual(manifest["filesystem"], {"start": "0x106ff000", "size": 1048576})
        fs_start = int(manifest["filesystem"]["start"], 16)
        self.assertLess(int(manifest["image"]["address_end"], 16), fs_start)
        self.assertEqual(sorted(sums), ["Orbit12-0.1.0-pre.1.uf2", "release-manifest.json"])
        self.assertEqual(sums[f"Orbit12-{VERSION}.uf2"], manifest["image"]["sha256"])
        self.assertEqual(sums["release-manifest.json"], hashlib.sha256(
            (json.dumps(manifest, indent=2) + "\n").encode("utf-8")).hexdigest())


class BuildScriptMatchesCli(unittest.TestCase):
    def test_build_firmware_manifest_flags_are_accepted_by_the_tool(self) -> None:
        script = os.path.join(TOOLS_DIR, "build-firmware.sh")
        with open(script, encoding="utf-8") as fh:
            text = fh.read()
        start = text.index("manifest_args=(")
        block = text[start:text.index(")", start)]
        flags = set(re.findall(r"--[a-z][a-z0-9-]*", block))
        extra = re.findall(r'manifest_args\+=\((--[a-z][a-z0-9-]*)', text)
        flags |= set(extra)
        self.assertTrue({"--uf2", "--elf", "--nm", "--version", "--git-sha", "--out-dir"} <= flags, flags)
        accepted = set(mrm.build_parser()._option_string_actions)
        self.assertEqual(flags - accepted, set(), "build-firmware.sh passes flags the tool does not accept")
        self.assertTrue({"--repo", "--allow-dirty"} <= flags, flags)


if __name__ == "__main__":
    unittest.main()
