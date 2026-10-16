"""UF2 structure checks: magic, family, flags, bounds, overlap with the filesystem, block numbering."""

import struct
import unittest

from helpers import ADDRESS_END, FLASH_START, FS_SIZE, FS_START, make_uf2

from orbit12_installer import uf2


def check(data, **overrides):
    options = dict(address_start=FLASH_START, address_end=ADDRESS_END, fs_start=FS_START, fs_size=FS_SIZE)
    options.update(overrides)
    return uf2.check_image(data, **options)


def patch_block(data, index, offset, value_format, value):
    block = bytearray(data[index * 512:(index + 1) * 512])
    struct.pack_into(value_format, block, offset, value)
    return bytes(data[:index * 512]) + bytes(block) + bytes(data[(index + 1) * 512:])


class Uf2CheckTests(unittest.TestCase):
    def test_valid_image_passes_and_reports_its_range(self):
        summary = check(make_uf2())
        self.assertEqual(summary.blocks, 4)
        self.assertEqual(summary.first_target, FLASH_START)
        self.assertEqual(summary.end_target, FLASH_START + 3 * 256 + 256)
        self.assertEqual(summary.payload_bytes, 4 * 256)

    def test_wrong_family_id_is_refused(self):
        with self.assertRaisesRegex(uf2.Uf2Error, "family ID"):
            check(make_uf2(family=0x00000000))

    def test_missing_family_flag_is_refused(self):
        with self.assertRaisesRegex(uf2.Uf2Error, "no family ID"):
            check(make_uf2(flags=0))

    def test_not_main_flash_flag_is_refused(self):
        with self.assertRaisesRegex(uf2.Uf2Error, "NOT_MAIN_FLASH"):
            check(make_uf2(flags=0x2001))

    def test_bad_magic_is_refused(self):
        data = patch_block(make_uf2(), 2, 0, "<I", 0xDEADBEEF)
        with self.assertRaisesRegex(uf2.Uf2Error, "magic"):
            check(data)

    def test_bad_end_magic_is_refused(self):
        data = patch_block(make_uf2(), 1, 508, "<I", 0)
        with self.assertRaisesRegex(uf2.Uf2Error, "magic"):
            check(data)

    def test_payload_size_above_476_is_refused(self):
        data = patch_block(make_uf2(), 0, 16, "<I", 477)
        with self.assertRaisesRegex(uf2.Uf2Error, "payload size"):
            check(data)

    def test_block_numbers_must_be_in_sequence(self):
        data = patch_block(make_uf2(), 1, 20, "<I", 5)
        with self.assertRaisesRegex(uf2.Uf2Error, "out of sequence"):
            check(data)

    def test_block_count_must_match_the_manifest(self):
        with self.assertRaisesRegex(uf2.Uf2Error, "manifest says 5"):
            check(make_uf2(), expected_blocks=5)

    def test_address_outside_the_image_range_is_refused(self):
        data = make_uf2(addresses=[FLASH_START + 0x800000])  # beyond the 8 MiB flash
        with self.assertRaisesRegex(uf2.Uf2Error, "outside the image range"):
            check(data)

    def test_block_tail_past_the_image_end_is_refused_even_if_the_start_is_inside(self):
        data = make_uf2(addresses=[ADDRESS_END - 128])  # starts inside, payload runs 128 bytes past
        with self.assertRaisesRegex(uf2.Uf2Error, "outside the image range"):
            check(data)

    def test_overlap_with_the_filesystem_partition_is_refused(self):
        data = make_uf2(addresses=[FS_START - 128])  # payload ends inside the partition
        with self.assertRaisesRegex(uf2.Uf2Error, "overlaps the filesystem"):
            check(data)

    def test_last_byte_just_before_the_partition_is_allowed(self):
        data = make_uf2(addresses=[FS_START - 256])
        check(data)

    def test_size_must_be_a_multiple_of_512(self):
        with self.assertRaisesRegex(uf2.Uf2Error, "512"):
            check(make_uf2()[:-1])

    def test_empty_image_is_refused(self):
        with self.assertRaises(uf2.Uf2Error):
            check(b"")


if __name__ == "__main__":
    unittest.main()
