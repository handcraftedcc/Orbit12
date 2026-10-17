"""Framing, status parsing and the bounded maintenance client (cdc.py)."""

import importlib.util
import os
import unittest

from helpers import REPO, FakeClock, FakePort, FakeWorld, encode_frame

from orbit12_installer import cdc


class FramingTests(unittest.TestCase):
    def test_golden_frames_match_the_documented_bytes(self):
        # Golden values from docs/migration/storage.md and scene_transfer.py selftest.
        self.assertEqual(cdc.build_frame(cdc.TYPE_FORMAT, cdc.FORMAT_TOKEN).hex().upper(),
                         "4F3146080000000045524153452D46533202957C")
        self.assertEqual(cdc.build_frame(cdc.TYPE_GET, b"Scene01").hex().upper(),
                         "4F31470700000000" + "5363656E653031" + "68A9ED1D")
        put = cdc.build_frame(cdc.TYPE_PUT, b"user_settings", b"key_brightness,0.5\n")
        self.assertEqual(put.hex().upper(), "4F3150" + "0D" + "13000000" + "757365725F73657474696E6773"
                         + "6B65795F6272696768746E6573732C302E350A" + "D13B1D37")

    def test_cdc_encoder_agrees_with_the_independent_fake_encoder(self):
        for args in [(cdc.TYPE_LIST, b"", b""), (cdc.TYPE_PUT, b"scene_names", b"01,ALFA\n"),
                     (cdc.TYPE_FORMAT, cdc.FORMAT_TOKEN, b"")]:
            self.assertEqual(cdc.build_frame(*args), encode_frame(*args))

    def test_parse_round_trip_and_tamper_detection(self):
        data = bytearray(cdc.build_frame(cdc.TYPE_PUT, b"user_settings", b"key_brightness,0.5\n"))
        frame = cdc.parse_frame(bytes(data))
        self.assertEqual((frame.kind, frame.name, frame.payload),
                         ("P", b"user_settings", b"key_brightness,0.5\n"))
        data[30] ^= 1
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_frame(bytes(data))

    def test_name_length_above_limit_is_refused(self):
        with self.assertRaises(ValueError):
            cdc.build_frame(ord("G"), b"x" * 17)

    def test_allowlist_rejects_traversal_and_temp_names(self):
        for bad in ("../Scene01", "Scene00", "Scene17", "scene01", "Scene01.tmp", "user_settings.bak"):
            self.assertNotIn(bad, cdc.ALLOWED_NAMES)
        for good in ("Scene01", "Scene16", "scene_names", "_activescene", "user_settings"):
            self.assertIn(good, cdc.ALLOWED_NAMES)

    def test_restore_order_puts_names_before_pointer_and_settings_last(self):
        order = cdc.RESTORE_ORDER
        self.assertLess(order.index("scene_names"), order.index("_activescene"))
        self.assertEqual(order[-1], "user_settings")
        self.assertEqual(sorted(order), sorted(cdc.ALLOWED_NAMES))

    def test_local_validation_matches_device_bounds(self):
        with self.assertRaises(ValueError):
            cdc.validate_local("Scene01", b"x" * (cdc.SCENE_MAX_BYTES + 1))
        cdc.validate_local("_activescene", b"16\n")
        for bad in (b"17\n", b"0\n"):
            with self.assertRaises(ValueError):
                cdc.validate_local("_activescene", bad)
        cdc.validate_local("scene_names", b"01,ALFA\r\n16,ZULU\n")
        for bad in (b"01,alfa\n", b"01,ALFA\n02,ALFA\n", b"01,ALFA\n01,BRAVO\n", b"1,ALFA\n"):
            with self.assertRaises(ValueError):
                cdc.validate_local("scene_names", bad)


class StatusParsingTests(unittest.TestCase):
    """The firmware contract: VERSION fw=<version> git=<sha> engine_api=<n> board=<id>."""

    DUMP = ("ORBIT12-SLICE up_ms=1\nSTORAGE mounted=1 mount_heap=0\n"
            "VERSION fw={fw} git={git} engine_api={api} board=adafruit_macropad_rp2040\n")

    def dump(self, fw="0.1.0-pre.1", git="abcdef1", api="1"):
        return self.DUMP.format(fw=fw, git=git, api=api)

    def test_version_line_is_read_from_its_key_value_fields(self):
        status = cdc.parse_dump(self.dump())
        self.assertTrue(status.orbit12)
        self.assertTrue(status.storage_mounted)
        self.assertEqual(status.version.semver, "0.1.0-pre.1")
        self.assertEqual(status.version.git_sha, "abcdef1")
        self.assertEqual(status.version.api, 1)
        self.assertEqual(status.version.board, "adafruit_macropad_rp2040")
        self.assertIsNone(status.version_problem)

    def test_dirty_and_dev_builds_are_read_as_reported(self):
        status = cdc.parse_dump(self.dump(fw="0.0.0-dev", git="abcdef1-dirty"))
        self.assertEqual(status.version.semver, "0.0.0-dev")
        self.assertEqual(status.version.git_sha, "abcdef1-dirty")

    def test_key_order_does_not_matter(self):
        text = "STORAGE mounted=0 mount_heap=0\nVERSION board=b engine_api=2 git=cafe123 fw=1.2.3\n"
        version = cdc.parse_dump(text).version
        self.assertEqual((version.semver, version.git_sha, version.api, version.board),
                         ("1.2.3", "cafe123", 2, "b"))

    def test_missing_version_line_is_tolerated_without_a_problem(self):
        status = cdc.parse_dump("STORAGE mounted=0 mount_heap=0\n")
        self.assertTrue(status.orbit12)
        self.assertFalse(status.storage_mounted)
        self.assertIsNone(status.version)
        self.assertIsNone(status.version_problem)

    def test_malformed_version_line_is_reported_as_a_problem_not_a_version(self):
        cases = [
            "VERSION fw=0.1.0 git=abc board=b\n",           # no engine_api
            "VERSION 0.1.0 abc api=1 board=b\n",            # the old positional grammar
            "VERSION fw=0.1.0 git=abc engine_api=x board=b\n",
            "VERSION fw=0.1.0 fw=0.2.0 git=abc engine_api=1 board=b\n",
            "VERSION fw= git=abc engine_api=1 board=b\n",
        ]
        for text in cases:
            status = cdc.parse_dump("STORAGE mounted=1 mount_heap=0\n" + text)
            self.assertIsNone(status.version, text)
            self.assertIsNotNone(status.version_problem, text)

    def test_circuitpython_text_is_not_orbit12(self):
        status = cdc.parse_dump("Adafruit CircuitPython 9.2.0\n>>> ")
        self.assertFalse(status.orbit12)
        self.assertTrue(status.circuitpython)
        self.assertIsNone(status.storage_mounted)


class ReadDumpTests(unittest.TestCase):
    def test_performance_mode_answers_the_dump(self):
        clock = FakeClock()
        world = FakeWorld(clock)
        board = world.add("COM9", mounted=True)
        text = cdc.read_dump(FakePort(board, clock), clock)
        self.assertIn("STORAGE mounted=1", text)

    def test_silent_device_returns_empty_within_the_deadline(self):
        clock = FakeClock()
        world = FakeWorld(clock)
        board = world.add("COM9", mounted=True)
        board.mode = "maintenance"  # maintenance ignores '?'
        start = clock.monotonic()
        text = cdc.read_dump(FakePort(board, clock), clock, deadline_s=1.0)
        self.assertEqual(text, "")
        self.assertLessEqual(clock.monotonic() - start, 1.5)


class MaintenanceClientTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM9", mounted=True,
                                    files={"Scene01": b"SCN1 data", "user_settings": b"key_brightness,1\n"})
        self.board.mode = "maintenance"
        self.port = FakePort(self.board, self.clock)
        self.link = cdc.MaintenanceLink(self.port)

    def test_list_parses_allowlisted_lines(self):
        listing = cdc.list_files(self.link)
        self.assertTrue(listing["Scene01"])
        self.assertFalse(listing["Scene02"])
        self.assertTrue(listing["user_settings"])

    def test_get_returns_bytes_and_none_for_not_found(self):
        self.assertEqual(cdc.get_file(self.link, "Scene01"), b"SCN1 data")
        self.assertIsNone(cdc.get_file(self.link, "Scene02"))

    def test_put_refused_is_an_error_naming_the_reason(self):
        self.board.refuse_puts = True
        with self.assertRaises(cdc.DeviceRefused) as caught:
            cdc.put_file(self.link, "Scene01", b"SCN1 new")
        self.assertEqual(caught.exception.reason, 7)

    def test_put_rejects_oversize_locally_without_sending(self):
        before = len(self.world.log)
        with self.assertRaises(ValueError):
            cdc.put_file(self.link, "Scene01", b"x" * (cdc.SCENE_MAX_BYTES + 1))
        self.assertEqual(len(self.world.log), before)

    def test_unmounted_list_is_a_nak_seven(self):
        self.board.mounted = False
        with self.assertRaises(cdc.DeviceRefused) as caught:
            cdc.list_files(self.link)
        self.assertEqual(caught.exception.reason, 7)

    def test_format_needs_the_exact_token_and_acks(self):
        cdc.format_partition(self.link)
        self.assertEqual(self.board.files, {})
        self.assertTrue(self.board.mounted)

    def test_format_failure_leaves_partition_unmounted(self):
        self.board.format_fails = True
        with self.assertRaises(cdc.DeviceRefused) as caught:
            cdc.format_partition(self.link)
        self.assertEqual(caught.exception.reason, 11)
        self.assertFalse(self.board.mounted)

    def test_silent_older_firmware_times_out_instead_of_hanging(self):
        self.board.info_supported = False  # older firmware ignores frames in performance mode
        self.board.mode = "normal"
        with self.assertRaises(TimeoutError):
            cdc.list_files(cdc.MaintenanceLink(FakePort(self.board, self.clock)))

    def test_current_firmware_refuses_data_requests_in_performance_mode_with_nak_14(self):
        self.board.mode = "normal"
        with self.assertRaises(cdc.DeviceRefused) as caught:
            cdc.list_files(cdc.MaintenanceLink(FakePort(self.board, self.clock)))
        self.assertEqual(caught.exception.reason, cdc.NAK_NOT_IN_MAINTENANCE)

    def test_response_payload_bound_is_enforced(self):
        header = b"\x4f\x31" + bytes([ord("D"), 0]) + (cdc.MAX_RESPONSE_PAYLOAD + 1).to_bytes(4, "little")
        port = FakePort(self.board, self.clock)
        port.rx += header
        with self.assertRaises(cdc.ProtocolError):
            cdc.read_frame(port)


class HostCommandTests(unittest.TestCase):
    """Package D frames: INFO, ENTER, EXIT. Goldens are the bytes in scene_transfer.py selftest."""

    def test_golden_host_frames_match_the_documented_bytes(self):
        self.assertEqual(cdc.build_frame(cdc.TYPE_INFO).hex().upper(), "4F314900000000006AFADCB3")
        self.assertEqual(cdc.build_frame(cdc.TYPE_ENTER, cdc.ENTER_TOKEN).hex().upper(),
                         "4F314D0B00000000454E5445522D4D41494E54A95EA0C5")
        self.assertEqual(cdc.build_frame(cdc.TYPE_EXIT, cdc.EXIT_TOKEN).hex().upper(),
                         "4F31450A00000000455849542D4D41494E54F2E28AB3")
        self.assertEqual(cdc.build_frame(ord("A"), cdc.ENTER_TOKEN).hex().upper(),
                         "4F31410B00000000454E5445522D4D41494E54F836B850")
        self.assertEqual(cdc.build_frame(ord("N"), cdc.ENTER_TOKEN, bytes([16])).hex().upper(),
                         "4F314E0B01000000454E5445522D4D41494E541008089424")

    def test_host_frames_agree_with_the_independent_encoder(self):
        self.assertEqual(cdc.build_frame(cdc.TYPE_INFO), encode_frame(cdc.TYPE_INFO))
        self.assertEqual(cdc.build_frame(cdc.TYPE_ENTER, cdc.ENTER_TOKEN),
                         encode_frame(cdc.TYPE_ENTER, cdc.ENTER_TOKEN))

    def test_nak_reason_names_cover_the_documented_codes(self):
        for code in (1, 2, 5, 10, 14, 15, 16, 17, 18):
            self.assertIn(code, cdc.NAK_REASONS)
        self.assertEqual(cdc.NAK_NOT_STOPPED, 16)
        self.assertEqual(cdc.NAK_BUSY, 5)
        self.assertEqual(cdc.NAK_ALREADY_IN_MAINTENANCE, 15)
        self.assertEqual(cdc.NAK_NOT_IN_MAINTENANCE, 14)


INFO_TEXT = (
    "INFO fw=0.1.0-pre.1 sha=abcdef123456 api=1 board=adafruit_macropad_rp2040\n"
    "MODULES count=2 core=transpose,chords user=\n"
    "STORAGE mounted=1 scenes=3 names_ok=1\n"
    "STATE maint=0 transport=1\n"
).encode("ascii")


class InfoParsingTests(unittest.TestCase):
    def test_all_sections_are_read(self):
        report = cdc.parse_info(INFO_TEXT)
        self.assertEqual((report.fw, report.sha, report.api, report.board),
                         ("0.1.0-pre.1", "abcdef123456", 1, "adafruit_macropad_rp2040"))
        self.assertEqual(report.module_count, 2)
        self.assertEqual(report.core_modules, ("transpose", "chords"))
        self.assertEqual(report.user_modules, ())
        self.assertTrue(report.storage_mounted)
        self.assertEqual(report.scenes, 3)
        self.assertTrue(report.names_ok)
        self.assertFalse(report.maintenance)
        self.assertTrue(report.transport)

    def test_dirty_and_dev_builds_are_read_as_reported(self):
        report = cdc.parse_info(INFO_TEXT.replace(b"fw=0.1.0-pre.1 sha=abcdef123456",
                                                  b"fw=0.0.0-dev sha=abcdef1-dirty"))
        self.assertEqual((report.fw, report.sha), ("0.0.0-dev", "abcdef1-dirty"))

    def test_unknown_sections_are_ignored(self):
        report = cdc.parse_info(INFO_TEXT + b"FUTURE x=1\n")
        self.assertEqual(report.fw, "0.1.0-pre.1")

    def test_missing_required_lines_or_keys_are_protocol_errors(self):
        lines = INFO_TEXT.decode("ascii").splitlines()
        for drop in range(len(lines)):
            with self.assertRaises(cdc.ProtocolError, msg=lines[drop]):
                cdc.parse_info("\n".join(lines[:drop] + lines[drop + 1:]).encode("ascii"))
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT.replace(b" names_ok=1", b""))

    def test_a_repeated_section_is_refused(self):
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT + b"STATE maint=0 transport=0\n")

    def test_flags_must_be_zero_or_one(self):
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT.replace(b"maint=0", b"maint=2"))

    def test_api_must_be_a_number(self):
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT.replace(b"api=1", b"api=one"))

    def test_longer_than_512_bytes_is_refused(self):
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT + b" " * 512)

    def test_non_ascii_is_refused(self):
        with self.assertRaises(cdc.ProtocolError):
            cdc.parse_info(INFO_TEXT + "é".encode("utf-8"))


@unittest.skipUnless(os.path.exists(os.path.join(REPO, "tools", "arduino", "scene_transfer.py")),
                     "repository copy of scene_transfer.py not present")
class HostDriftTests(unittest.TestCase):
    def test_host_constants_and_nak_table_match_scene_transfer(self):
        path = os.path.join(REPO, "tools", "arduino", "scene_transfer.py")
        spec = importlib.util.spec_from_file_location("scene_transfer_host", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.TYPE_INFO, cdc.TYPE_INFO)
        self.assertEqual(module.TYPE_ENTER, cdc.TYPE_ENTER)
        self.assertEqual(module.TYPE_EXIT, cdc.TYPE_EXIT)
        self.assertEqual(module.ENTER_TOKEN, cdc.ENTER_TOKEN)
        self.assertEqual(module.EXIT_TOKEN, cdc.EXIT_TOKEN)
        self.assertEqual(module.TYPE_RENAME, cdc.TYPE_RENAME)
        self.assertEqual(module.rename_line(3, "ALFA"), cdc.rename_line(3, "ALFA"))
        self.assertTrue(module.info_has_rename(INFO_TEXT + b"CAPS rename=1\n"))
        self.assertEqual(sorted(module.NAK_REASONS), sorted(cdc.NAK_REASONS))
        self.assertEqual(module.build_frame(module.TYPE_ENTER, module.ENTER_TOKEN),
                         cdc.build_frame(cdc.TYPE_ENTER, cdc.ENTER_TOKEN))


@unittest.skipUnless(os.path.exists(os.path.join(REPO, "tools", "arduino", "scene_transfer.py")),
                     "repository copy of scene_transfer.py not present")
class DriftTests(unittest.TestCase):
    def test_vendored_framing_matches_scene_transfer(self):
        path = os.path.join(REPO, "tools", "arduino", "scene_transfer.py")
        spec = importlib.util.spec_from_file_location("scene_transfer_under_test", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.SCENE_MAX_BYTES, cdc.SCENE_MAX_BYTES)
        self.assertEqual(module.FORMAT_TOKEN, cdc.FORMAT_TOKEN)
        self.assertEqual(sorted(module.ALLOWED), sorted(cdc.ALLOWED_NAMES))
        for args in [(cdc.TYPE_LIST, b"", b""), (cdc.TYPE_GET, b"Scene07", b""),
                     (cdc.TYPE_PUT, b"scene_names", b"01,ALFA\n")]:
            self.assertEqual(cdc.build_frame(*args), module.build_frame(*args))


class RenameFramingTests(unittest.TestCase):
    def test_golden_rename_frame_matches_the_documented_bytes(self):
        # Golden from the scene_transfer.py selftest: the name field carries the "NN,NAME" line.
        self.assertEqual(cdc.build_frame(cdc.TYPE_RENAME, b"01,ALFA").hex().upper(),
                         "4F3152070000000030312C414C4641561493FC")

    def test_rename_line_accepts_only_the_names_grammar(self):
        self.assertEqual(cdc.rename_line(1, "ALFA"), b"01,ALFA")
        self.assertEqual(cdc.rename_line(16, "ZULU8"), b"16,ZULU8")
        for slot, name in ((0, "ALFA"), (17, "ALFA"), (True, "ALFA"), (1, "alfa"), (1, ""),
                           (1, "ABCDEFGHI"), (1, "A B")):
            with self.assertRaises(ValueError):
                cdc.rename_line(slot, name)

    def test_name_checks_give_plain_reasons(self):
        self.assertIsNone(cdc.scene_name_problem("ZULU", taken=("ALFA",)))
        self.assertIn("8 characters", cdc.scene_name_problem("ABCDEFGHI"))
        self.assertIn("A-Z and digits", cdc.scene_name_problem("A-B"))
        self.assertIn("already used", cdc.scene_name_problem("ALFA", taken=("ALFA",)))
        self.assertIn("1 to 8", cdc.scene_name_problem(""))
        self.assertEqual(cdc.normalize_scene_name("  zulu "), "ZULU")

    def test_caps_rename_flag_is_optional_and_any_value_but_one_means_no(self):
        self.assertTrue(cdc.parse_info(INFO_TEXT + b"CAPS rename=1\n").renames)
        self.assertFalse(cdc.parse_info(INFO_TEXT).renames)  # older firmware: no CAPS line
        self.assertFalse(cdc.parse_info(INFO_TEXT + b"CAPS rename=0\n").renames)
        self.assertFalse(cdc.parse_info(INFO_TEXT + b"CAPS rename=yes\n").renames)
        # A CAPS line never makes the rest of INFO unreadable.
        self.assertTrue(cdc.parse_info(INFO_TEXT + b"CAPS rename=1\n").names_ok)

    def test_rename_nak_reasons_are_named(self):
        for code in (19, 20, 21, 22):
            self.assertIn(code, cdc.NAK_REASONS)

    def test_rename_sends_the_line_and_reports_the_device_reason(self):
        clock = FakeClock()
        world = FakeWorld(clock)
        board = world.add("COM9", mounted=True, files={"scene_names": b"01,ALFA\n03,BRAVO\n"})
        board.mode = "maintenance"
        link = cdc.MaintenanceLink(FakePort(board, clock))
        cdc.rename_scene(link, 1, "ZULU")
        self.assertEqual(board.files["scene_names"], b"01,ZULU\n03,BRAVO\n")
        with self.assertRaises(cdc.DeviceRefused) as caught:
            cdc.rename_scene(link, 3, "ZULU")  # ZULU is now held by slot 01
        self.assertEqual(caught.exception.reason, 20)
        self.assertEqual(board.files["scene_names"], b"01,ZULU\n03,BRAVO\n")


if __name__ == "__main__":
    unittest.main()
