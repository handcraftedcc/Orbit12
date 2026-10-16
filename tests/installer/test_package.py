"""Manifest, SHA256SUMS, path safety and whole-image verification (package.py)."""

import json
import unittest

from helpers import make_uf2, manifest_doc, sha256, sums_text

from orbit12_installer import package
from orbit12_installer.package import PackageError


def parse(**overrides):
    return package.parse_manifest(manifest_doc(make_uf2(), **overrides))


class ManifestTests(unittest.TestCase):
    def test_valid_manifest_parses(self):
        manifest = parse()
        self.assertEqual(manifest.version, "0.1.0-pre.1")
        self.assertEqual(manifest.image.file, "Orbit12-0.1.0-pre.1.uf2")
        self.assertEqual(manifest.fs_start, 0x106ff000)
        self.assertEqual(manifest.image.blocks, 4)

    def test_wrong_board_is_refused(self):
        with self.assertRaisesRegex(PackageError, "board"):
            parse(board="adafruit_feather_rp2040")

    def test_wrong_chip_or_family_is_refused(self):
        with self.assertRaisesRegex(PackageError, "chip"):
            parse(chip="RP2350")
        with self.assertRaisesRegex(PackageError, "family"):
            parse(uf2_family_id="0x00000000")

    def test_schema_other_than_one_is_refused(self):
        with self.assertRaisesRegex(PackageError, "schema"):
            parse(schema=2)

    def test_repo_mismatch_is_refused_when_expected(self):
        image = make_uf2()
        data = manifest_doc(image)
        with self.assertRaisesRegex(PackageError, "does not match"):
            package.parse_manifest(data, expected_repo="someone/fork")
        package.parse_manifest(data, expected_repo="HandcraftedCC/Orbit12")

    def test_image_name_must_match_the_version(self):
        with self.assertRaisesRegex(PackageError, "does not match its version"):
            parse(image={"file": "Orbit12-0.2.0.uf2"})

    def test_image_name_with_a_path_is_refused(self):
        with self.assertRaises(PackageError):
            parse(image={"file": "../Orbit12-0.1.0-pre.1.uf2"})

    def test_block_count_must_agree_with_size(self):
        with self.assertRaisesRegex(PackageError, "block count"):
            parse(image={"blocks": 99})

    def test_image_window_must_lie_inside_the_xip_window(self):
        with self.assertRaisesRegex(PackageError, "flash window"):
            parse(image={"address_start": "0x0F000000"})
        with self.assertRaisesRegex(PackageError, "flash window"):
            parse(image={"address_end": "0x11000001"})

    def test_real_clean_build_values_are_accepted(self):
        # The values from a clean release build: 1072 blocks of 256 bytes, filesystem at the top.
        manifest = parse(image={"size": 548864, "blocks": 1072, "address_end": "0x10043000"},
                         filesystem={"start": "0x106ff000", "size": 1048576})
        self.assertEqual(manifest.image.address_end, 0x10043000)
        self.assertEqual(manifest.fs_start, 0x106FF000)
        self.assertEqual(manifest.fs_size, 1048576)

    def test_filesystem_outside_the_xip_window_is_refused(self):
        with self.assertRaisesRegex(PackageError, "filesystem window"):
            parse(filesystem={"start": "0x10F00000", "size": 0x200000})  # ends past 0x11000000

    def test_image_overlapping_the_filesystem_is_refused(self):
        with self.assertRaisesRegex(PackageError, "overlap"):
            parse(image={"address_end": "0x10700000"}, filesystem={"start": "0x106ff000", "size": 1048576})

    def test_unaligned_filesystem_start_is_refused(self):
        with self.assertRaisesRegex(PackageError, "aligned"):
            parse(filesystem={"start": "0x106ff001", "size": 1048576})

    def test_zero_or_odd_filesystem_size_is_refused(self):
        with self.assertRaises(PackageError):
            parse(filesystem={"start": "0x106ff000", "size": 0})
        with self.assertRaisesRegex(PackageError, "multiple of 4096"):
            parse(filesystem={"start": "0x106ff000", "size": 1000})
        with self.assertRaises(PackageError):
            parse(filesystem={"start": "0x106ff000", "size": -4096})

    def test_bad_json_and_oversize_are_refused(self):
        with self.assertRaisesRegex(PackageError, "JSON"):
            package.parse_manifest(b"{not json")
        with self.assertRaisesRegex(PackageError, "larger"):
            package.parse_manifest(b" " * (package.MAX_MANIFEST_BYTES + 1))

    def test_tag_must_agree_with_version_with_optional_v(self):
        self.assertTrue(package.tag_matches_version("v0.1.0-pre.1", "0.1.0-pre.1"))
        self.assertTrue(package.tag_matches_version("0.1.0-pre.1", "0.1.0-pre.1"))
        self.assertFalse(package.tag_matches_version("v0.1.0", "0.1.0-pre.1"))


class SumsTests(unittest.TestCase):
    def test_parses_text_and_binary_marker_lines(self):
        digest = "a" * 64
        entries = package.parse_sha256sums(f"{digest}  Orbit12-0.1.0.uf2\n{'b' * 64} *other.bin\n".encode())
        self.assertEqual(entries["Orbit12-0.1.0.uf2"], digest)
        self.assertIn("other.bin", entries)

    def test_duplicate_entries_are_refused(self):
        line = f"{'a' * 64}  file.uf2\n"
        with self.assertRaisesRegex(PackageError, "twice"):
            package.parse_sha256sums((line * 2).encode())

    def test_malformed_lines_are_refused(self):
        with self.assertRaisesRegex(PackageError, "malformed"):
            package.parse_sha256sums(b"not-a-hash  file.uf2\n")

    def test_path_traversal_in_names_is_refused(self):
        for bad in ("../evil.uf2", "sub/evil.uf2", "..\\evil.uf2", "C:evil.uf2"):
            with self.assertRaises(PackageError):
                package.parse_sha256sums(f"{'a' * 64}  {bad}\n".encode())

    def test_oversize_list_is_refused(self):
        with self.assertRaisesRegex(PackageError, "larger"):
            package.parse_sha256sums(b"x" * (package.MAX_SUMS_BYTES + 1))


class SafeNameTests(unittest.TestCase):
    def test_accepts_bare_names_and_rejects_everything_else(self):
        self.assertEqual(package.safe_name("Orbit12-0.1.0-pre.1.uf2"), "Orbit12-0.1.0-pre.1.uf2")
        for bad in ("", ".hidden", "../x", "a/b", "a\\b", "x..y/../z", "C:\\x", "con\x00"):
            with self.assertRaises(PackageError, msg=bad):
                package.safe_name(bad)


class ImageVerificationTests(unittest.TestCase):
    def setUp(self):
        self.image = make_uf2()
        self.manifest = package.parse_manifest(manifest_doc(self.image))
        self.good_sums = package.parse_sha256sums(sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(self.image)}))

    def test_good_image_with_consistent_sums_verifies(self):
        package.verify_image_bytes(self.image, self.manifest, self.good_sums)
        package.verify_image_bytes(self.image, self.manifest, None)

    def test_bad_hash_is_refused(self):
        tampered = bytearray(self.image)
        tampered[100] ^= 0xFF
        with self.assertRaisesRegex(PackageError, "does not match"):
            package.verify_image_bytes(bytes(tampered), self.manifest, None)

    def test_tampered_sha256sums_is_refused(self):
        sums = dict(self.good_sums)
        sums["Orbit12-0.1.0-pre.1.uf2"] = "0" * 64
        with self.assertRaisesRegex(PackageError, "disagrees with the downloaded image"):
            package.verify_image_bytes(self.image, self.manifest, sums)

    def test_sums_missing_the_image_is_refused(self):
        with self.assertRaisesRegex(PackageError, "no entry"):
            package.verify_image_bytes(self.image, self.manifest, {})

    def test_sums_and_manifest_disagreeing_is_refused_even_when_the_image_matches_one(self):
        other = make_uf2(addresses=[0x10000000])
        sums = {"Orbit12-0.1.0-pre.1.uf2": sha256(other)}
        with self.assertRaises(PackageError):
            package.verify_image_bytes(other, self.manifest, sums)

    def test_size_mismatch_is_refused(self):
        with self.assertRaisesRegex(PackageError, "bytes"):
            package.verify_image_bytes(self.image + b"\0" * 512, self.manifest, None)

    def test_structurally_bad_image_with_matching_hash_is_refused(self):
        bad = make_uf2(family=0)
        manifest = package.parse_manifest(manifest_doc(bad))
        with self.assertRaisesRegex(PackageError, "UF2 check failed"):
            package.verify_image_bytes(bad, manifest, None)

    def test_image_overlapping_the_filesystem_is_refused_with_matching_hash(self):
        bad = make_uf2(addresses=[0x106ff000 - 128])
        manifest = package.parse_manifest(manifest_doc(bad))
        with self.assertRaisesRegex(PackageError, "outside the image range|overlaps"):
            package.verify_image_bytes(bad, manifest, None)


if __name__ == "__main__":
    unittest.main()
