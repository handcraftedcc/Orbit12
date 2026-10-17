"""Device operations against the fake world: discovery, maintenance, backup, restore, format, volumes, copy."""

import os
import shutil
import unittest

from helpers import (BOARD, OLD_VERSION, write_backup, FakeClock, FakeWorld, RecordingConsole, TempDirTest, hardware, make_uf2,
                     manifest_doc, scenes)

from orbit12_installer import cdc, device, package
from orbit12_installer.device import FlashError
from orbit12_installer.interfaces import Volume


def scenes():
    return {"Scene01": b"SCN1 one\n", "Scene03": b"SCN1 three\n", "scene_names": b"01,ALFA\n03,BRAVO\n",
            "_activescene": b"3\n", "user_settings": b"key_brightness,0.5\n"}


class DiscoveryTests(unittest.TestCase):
    def test_finds_exactly_one_orbit12_and_ignores_other_vids(self):
        world = FakeWorld()
        world.add("COM7")
        world.extra_ports.append(device_info("COM9", vid=0x1234))
        hw = hardware(world, RecordingConsole())
        found = device.find_orbit12(hw)
        self.assertEqual(found.port.device, "COM7")
        self.assertTrue(found.status.storage_mounted)

    def test_two_orbit12_boards_are_refused_not_guessed(self):
        world = FakeWorld()
        world.add("COM7")
        world.add("COM8")
        with self.assertRaisesRegex(FlashError, "several Orbit12"):
            device.find_orbit12(hardware(world, RecordingConsole()))

    def test_circuitpython_port_is_seen_but_not_orbit12(self):
        world = FakeWorld()
        world.add("COM7", legacy=True)
        hw = hardware(world, RecordingConsole())
        self.assertIsNone(device.find_orbit12(hw))
        self.assertTrue(device.has_other_cdc(hw))

    def test_port_override_picks_the_named_port(self):
        world = FakeWorld()
        world.add("COM7")
        world.add("COM8")
        found = device.find_orbit12(hardware(world, RecordingConsole()), override="com8")
        self.assertEqual(found.port.device, "COM8")


def device_info(name, vid):
    from orbit12_installer.interfaces import PortInfo
    return PortInfo(name, vid, 0x0001, "other")


class MaintenanceTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM7", files=scenes())
        self.hw = hardware(self.world, RecordingConsole())

    def enter_on_prompt(self, board=None):
        """The user holds a key for the pad-hold prompt: the board enters maintenance."""
        target = board or self.board

        def on_pause(message):
            if "hold any key" in message:
                target.mode = "maintenance"
        return on_pause

    def test_performance_mode_is_not_maintenance(self):
        self.assertFalse(device.maintenance_active(self.hw, "COM7"))

    def test_maintenance_is_detected_from_info_state(self):
        self.board.mode = "maintenance"
        self.assertTrue(device.maintenance_active(self.hw, "COM7"))

    def test_older_firmware_in_maintenance_is_detected_by_a_list_reply(self):
        self.board.info_supported = False
        self.board.mode = "maintenance"
        self.assertTrue(device.maintenance_active(self.hw, "COM7"))

    def test_discovery_reads_info_and_storage_from_the_board(self):
        orbit = device.find_orbit12(self.hw)
        self.assertIsNotNone(orbit.info)
        self.assertEqual(orbit.info.fw, OLD_VERSION)
        self.assertTrue(orbit.storage_mounted)

    def test_host_entry_needs_no_prompt_when_playback_is_stopped(self):
        console = RecordingConsole()
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertEqual(console.pauses, [])
        self.assertEqual(self.board.mode, "maintenance")
        self.assertIn("COM7:ENTER", self.world.log)

    def test_already_in_maintenance_sends_no_enter(self):
        self.board.mode = "maintenance"
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertNotIn("COM7:ENTER", self.world.log)

    def test_playback_running_prompts_to_stop_then_retries_after_the_user_stops_it(self):
        self.board.transport = True

        class StopsOnFirstWait(FakeClock):
            def sleep(inner, seconds):
                super().sleep(seconds)
                if seconds == device.ENTER_RETRY_INTERVAL_S:  # the retry wait, not a port timeout
                    self.board.transport = False  # the user stopped playback during the wait

        self.world.clock = StopsOnFirstWait()
        self.hw = hardware(self.world, RecordingConsole())
        self.hw.console.on_pause = lambda msg: None
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertTrue(any("Playback is running" in p for p in self.hw.console.pauses))
        self.assertEqual(self.world.log.count("COM7:ENTER"), 2)  # one NotStopped, then accepted
        self.assertTrue(any("trying again in 2 s" in t for t in self.hw.console.said))

    def test_busy_replies_are_retried_until_accepted(self):
        self.board.busy_enters = 2
        console = RecordingConsole()
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertEqual(self.world.log.count("COM7:ENTER"), 3)
        self.assertEqual(console.pauses, [])

    def test_playback_that_never_stops_falls_back_to_the_key_hold_after_20_seconds(self):
        self.board.transport = True
        console = RecordingConsole(on_pause=self.enter_on_prompt())
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        start = self.clock.monotonic()
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        attempts = self.world.log.count("COM7:ENTER")
        self.assertEqual(attempts, 11)  # at 0, 2, ... 20 seconds
        self.assertLessEqual(self.clock.monotonic() - start, 25)
        self.assertTrue(any("stayed busy" in w for w in console.warned))
        self.assertTrue(any("hold any key" in p for p in console.pauses))

    def test_lost_enter_reply_is_confirmed_by_asking_info(self):
        self.board.drop_enter_reply = True  # the board enters, but its ACK never arrives
        console = RecordingConsole()
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertEqual(console.pauses, [])
        self.assertEqual(self.board.mode, "maintenance")

    def test_firmware_without_info_uses_the_key_hold_and_never_sends_enter(self):
        self.board.info_supported = False
        console = RecordingConsole(on_pause=self.enter_on_prompt())
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        self.assertEqual(device.ensure_maintenance(self.hw, orbit), "COM7")
        self.assertEqual(len(console.pauses), 1)
        self.assertNotIn("COM7:ENTER", self.world.log)

    def test_ensure_gives_up_after_bounded_attempts_without_entering(self):
        self.board.info_supported = False
        console = RecordingConsole(yes=[True, True, True])
        self.hw.console = console
        orbit = device.find_orbit12(self.hw)
        self.assertIsNone(device.ensure_maintenance(self.hw, orbit, attempts=2))
        self.assertEqual(len(console.pauses), 2)

    def test_exit_uses_the_exit_frame_on_current_firmware(self):
        self.board.mode = "maintenance"
        device.exit_maintenance(self.hw, "COM7")
        self.assertEqual(self.board.mode, "normal")
        self.assertIn("COM7:EXIT", self.world.log)
        self.assertNotIn("COM7:Q", self.world.log)

    def test_exit_falls_back_to_the_q_byte_on_older_firmware(self):
        self.board.info_supported = False
        self.board.mode = "maintenance"
        device.exit_maintenance(self.hw, "COM7")
        self.assertEqual(self.board.mode, "normal")
        self.assertIn("COM7:Q", self.world.log)

    def test_exit_when_already_in_performance_mode_is_not_an_error(self):
        device.exit_maintenance(self.hw, "COM7")
        self.assertEqual(self.board.mode, "normal")


class BackupRestoreTests(TempDirTest):
    def setUp(self):
        super().setUp()
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM7", files=scenes())
        self.board.mode = "maintenance"
        self.console = RecordingConsole()
        self.hw = hardware(self.world, self.console)

    def test_backup_writes_every_present_file_and_a_verifying_manifest(self):
        folder = device.backup_device(self.hw, "COM7", self.tmp)
        names = sorted(os.listdir(folder))
        self.assertIn("backup.json", names)
        for name in scenes():
            self.assertTrue(os.path.exists(os.path.join(folder, name)), name)
        self.assertFalse(os.path.exists(os.path.join(folder, "Scene02")))
        restored = dict(device.load_backup(folder))
        self.assertEqual(restored["Scene03"], b"SCN1 three\n")

    def test_backup_of_an_empty_partition_writes_nothing(self):
        self.board.files.clear()
        self.assertIsNone(device.backup_device(self.hw, "COM7", self.tmp))
        self.assertEqual(os.listdir(self.tmp), [])

    def test_backup_of_an_unmounted_partition_is_none_with_a_warning(self):
        self.board.mounted = False
        self.assertIsNone(device.backup_device(self.hw, "COM7", self.tmp))
        self.assertTrue(any("nothing was backed up" in w for w in self.console.warned))

    def test_backup_refuses_to_restore_a_tampered_file(self):
        folder = device.backup_device(self.hw, "COM7", self.tmp)
        with open(os.path.join(folder, "Scene01"), "wb") as handle:
            handle.write(b"SCN1 changed\n")
        with self.assertRaisesRegex(FlashError, "does not match"):
            device.load_backup(folder)

    def test_incomplete_backup_without_manifest_is_refused(self):
        folder = os.path.join(self.tmp, "partial")
        os.makedirs(folder)
        with self.assertRaisesRegex(FlashError, "not a complete backup"):
            device.load_backup(folder)

    def test_restore_writes_names_before_pointer_and_settings_last(self):
        folder = device.backup_device(self.hw, "COM7", self.tmp)
        self.board.files.clear()
        self.world.log.clear()
        restored = device.restore_device(self.hw, "COM7", folder)
        puts = [entry.split(":PUT:")[1] for entry in self.world.log if ":PUT:" in entry]
        self.assertEqual(puts, ["Scene01", "Scene03", "scene_names", "_activescene", "user_settings"])
        self.assertEqual(restored, puts)
        self.assertEqual(self.board.files["scene_names"], b"01,ALFA\n03,BRAVO\n")

    def test_restore_stops_at_the_first_refusal(self):
        folder = device.backup_device(self.hw, "COM7", self.tmp)
        self.board.refuse_puts = True
        with self.assertRaises(cdc.DeviceRefused):
            device.restore_device(self.hw, "COM7", folder)
        puts = [entry for entry in self.world.log if ":PUT:" in entry]
        self.assertEqual(len(puts), 1)


class FormatTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM7", files=scenes())
        self.board.mode = "maintenance"

    def test_format_requires_the_typed_word_erase(self):
        console = RecordingConsole(typed=["erase"])
        hw = hardware(self.world, console)
        with self.assertRaises(device.Cancelled):
            device.format_device(hw, "COM7")
        self.assertNotIn("COM7:FORMAT", self.world.log)
        self.assertEqual(len(self.board.files), 5)
        self.assertIn("ERASE", console.typed_prompts)

    def test_yes_flag_never_answers_the_typed_confirmation(self):
        console = RecordingConsole(assume_yes=True)  # confirm() says yes; confirm_typed does not
        hw = hardware(self.world, console)
        with self.assertRaises(device.Cancelled):
            device.format_device(hw, "COM7")
        self.assertNotIn("COM7:FORMAT", self.world.log)

    def test_typed_erase_formats_and_verifies_an_empty_partition(self):
        console = RecordingConsole(typed=["ERASE"])
        device.format_device(hardware(self.world, console), "COM7")
        self.assertEqual(self.board.files, {})
        self.assertTrue(self.board.mounted)
        self.assertTrue(any("CIRCUITPY" in w for w in console.warned))

    def test_format_failure_is_reported_and_leaves_the_partition_unmounted(self):
        self.board.format_fails = True
        console = RecordingConsole(typed=["ERASE"])
        with self.assertRaisesRegex(FlashError, "refused"):
            device.format_device(hardware(self.world, console), "COM7")
        self.assertFalse(self.board.mounted)


class VolumeTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.hw = hardware(self.world, RecordingConsole())

    def test_one_bootsel_drive_is_found_after_a_touch(self):
        board = self.world.add("COM7")
        volume = device.enter_bootloader(self.hw, "COM7", timeout_s=5)
        self.assertEqual(volume.path, board.volume_path)
        self.assertIn("RPI-RP2", volume.info_text)
        shutil.rmtree(board.volume_path, ignore_errors=True)

    def test_no_drive_is_refused_after_the_timeout(self):
        start = self.clock.monotonic()
        with self.assertRaisesRegex(FlashError, "no RP2040 bootloader drive"):
            device.wait_for_bootsel(self.hw, timeout_s=3)
        self.assertGreaterEqual(self.clock.monotonic() - start, 3)

    def test_several_bootloader_drives_are_refused(self):
        a = self.world.add("COM7")
        b = self.world.add("COM8")
        a.touch()
        b.touch()
        with self.assertRaisesRegex(FlashError, "several RPI-RP2"):
            device.wait_for_bootsel(self.hw, timeout_s=5)
        for board in (a, b):
            shutil.rmtree(board.volume_path, ignore_errors=True)

    def test_a_removable_drive_without_the_marker_is_not_chosen(self):
        self.world.extra_volumes.append(Volume("E:\\", "Some other drive", frozenset({"photos"})))
        with self.assertRaises(FlashError):
            device.wait_for_bootsel(self.hw, timeout_s=1)

    def test_circuitpy_volume_is_identified_but_never_treated_as_bootsel(self):
        self.world.extra_volumes.append(Volume("F:\\", "", frozenset({"boot_out.txt", "code.py"})))
        self.assertEqual(len(device.circuitpy_volumes(self.hw)), 1)
        self.assertEqual(device.bootsel_volumes(self.hw), [])


class CopyTests(TempDirTest):
    def test_copy_writes_the_exact_bytes_under_a_fixed_name(self):
        image = make_uf2()
        volume = Volume(self.tmp, "RPI-RP2", frozenset())
        result = device.copy_image(volume, image)
        with open(os.path.join(self.tmp, "Orbit12.uf2"), "rb") as handle:
            self.assertEqual(handle.read(), image)
        self.assertEqual(result.bytes_written, len(image))
        self.assertFalse(result.pending)

    def test_interrupted_copy_is_reported_and_does_not_claim_success(self):
        image = make_uf2()
        volume = Volume(self.tmp, "RPI-RP2", frozenset())

        class Flaky:
            def __init__(self, path, mode, **kwargs):
                self.handle = open(path, mode, **kwargs)
                self.written = 0

            def write(self, data):
                if self.written >= 512:
                    raise OSError("device unplugged")
                self.written += len(data)
                return self.handle.write(data)

            def flush(self):
                self.handle.flush()

            def fileno(self):
                return self.handle.fileno()

            def close(self):
                self.handle.close()

        with self.assertRaisesRegex(device.CopyInterrupted, "stopped after"):
            device.copy_image(volume, image, opener=Flaky, chunk_bytes=256)

    def test_failure_after_every_byte_was_accepted_is_pending_not_interrupted(self):
        image = make_uf2()
        volume = Volume(self.tmp, "RPI-RP2", frozenset())

        class ClosesBadly:
            def __init__(self, path, mode, **kwargs):
                self.handle = open(path, mode, **kwargs)

            def write(self, data):
                return self.handle.write(data)

            def flush(self):
                pass

            def fileno(self):
                raise OSError("no descriptor")

            def close(self):
                self.handle.close()
                raise OSError("device reset")

        result = device.copy_image(volume, image, opener=ClosesBadly)
        self.assertTrue(result.pending)
        self.assertEqual(result.bytes_written, len(image))

    def test_size_mismatch_after_copy_is_refused(self):
        image = make_uf2()
        volume = Volume(self.tmp, "RPI-RP2", frozenset())

        class Short:
            def __init__(self, path, mode, **kwargs):
                self.handle = open(path, mode, **kwargs)

            def write(self, data):
                self.handle.write(data[:-1])  # the drive keeps one byte less than it was given
                return len(data)

            def flush(self):
                self.handle.flush()

            def fileno(self):
                return self.handle.fileno()

            def close(self):
                self.handle.close()

        with self.assertRaisesRegex(device.CopyInterrupted, "expected"):
            device.copy_image(volume, image, opener=Short)

    def test_board_that_never_reenumerates_is_reported(self):
        clock = FakeClock()
        world = FakeWorld(clock)
        board = world.add("COM7")
        board.reenumerates = False
        image = make_uf2()
        board.reboot_size = len(image)
        hw = hardware(world, RecordingConsole())
        board.touch()
        volume = device.wait_for_bootsel(hw, timeout_s=5)
        result = device.copy_image(volume, image)
        self.assertFalse(result.pending)
        self.assertTrue(device.wait_for_drive_gone(hw, volume.path, timeout_s=5))
        self.assertIsNone(device.wait_for_reenumeration(hw, timeout_s=3))


class RestoreValidationTests(TempDirTest):
    def test_a_backup_file_the_installer_refuses_becomes_a_flash_error(self):
        import hashlib
        import json

        clock = FakeClock()
        world = FakeWorld(clock)
        board = world.add("COM7")
        board.mode = "maintenance"
        bad = b"01,alfa\n"  # lower-case name: the strict local check refuses it
        with open(os.path.join(self.tmp, "scene_names"), "wb") as handle:
            handle.write(bad)
        doc = {"schema": 1, "files": [{"name": "scene_names", "size": len(bad),
                                       "sha256": hashlib.sha256(bad).hexdigest()}]}
        with open(os.path.join(self.tmp, "backup.json"), "w", encoding="utf-8") as handle:
            json.dump(doc, handle)
        hw = hardware(world, RecordingConsole())
        with self.assertRaisesRegex(device.FlashError, "refused by the installer"):
            device.restore_device(hw, "COM7", self.tmp)
        self.assertFalse(any(":PUT:" in entry for entry in world.log))


class VerifyTests(unittest.TestCase):
    """verify_firmware works from INFO (primary) or from the '?' dump (older firmware)."""

    def setUp(self):
        self.image = make_uf2()
        self.manifest = package.parse_manifest(manifest_doc(self.image))

    def info(self, fw="0.1.0-pre.1", sha="abcdef123456", api=1, board=BOARD, mounted=True):
        return cdc.InfoReport(fw=fw, sha=sha, api=api, board=board, module_count=1,
                              core_modules=("transpose",), user_modules=(), storage_mounted=mounted,
                              scenes=2, names_ok=True, maintenance=False, transport=False)

    def dev(self, info=None, status=None):
        from orbit12_installer.cdc import DeviceStatus
        from orbit12_installer.interfaces import PortInfo
        status = status or DeviceStatus(orbit12=True, storage_mounted=None, version=None)
        return device.Orbit12Device(PortInfo("COM7", 0x239A, 1, ""), status, info)

    def test_matching_info_passes_without_warnings(self):
        self.assertEqual(device.verify_firmware(self.dev(self.info()), self.manifest), [])

    def test_info_storage_state_is_used_for_the_format_decision(self):
        self.assertFalse(self.dev(self.info(mounted=False)).storage_mounted)
        self.assertTrue(self.dev(self.info(mounted=True)).storage_mounted)

    def test_dirty_build_of_the_same_commit_passes_with_a_warning(self):
        warnings = device.verify_firmware(self.dev(self.info(sha="abcdef123456-dirty")), self.manifest)
        self.assertTrue(any("uncommitted" in w for w in warnings))

    def test_dev_build_is_refused_against_a_release_manifest(self):
        with self.assertRaisesRegex(FlashError, "fw=0.0.0-dev"):
            device.verify_firmware(self.dev(self.info(fw="0.0.0-dev")), self.manifest)

    def test_wrong_board_api_version_or_git_fails(self):
        cases = [
            self.info(board="adafruit_feather"),
            self.info(api=2),
            self.info(fw="0.0.1"),
            self.info(sha="fedcba0"),
        ]
        for report in cases:
            with self.assertRaises(FlashError, msg=report):
                device.verify_firmware(self.dev(report), self.manifest)

    def test_older_firmware_is_verified_from_the_dump_with_a_warning(self):
        from orbit12_installer.cdc import DeviceStatus, VersionInfo
        status = DeviceStatus(orbit12=True, storage_mounted=True,
                              version=VersionInfo("0.1.0-pre.1", "abcdef123456", 1, BOARD))
        warnings = device.verify_firmware(self.dev(None, status), self.manifest)
        self.assertTrue(any("older firmware" in w for w in warnings))

    def test_older_firmware_without_any_version_warns_and_passes(self):
        from orbit12_installer.cdc import DeviceStatus
        status = DeviceStatus(orbit12=True, storage_mounted=None, version=None)
        warnings = device.verify_firmware(self.dev(None, status), self.manifest)
        self.assertTrue(any("neither INFO nor a VERSION" in w for w in warnings))

    def test_unreadable_dump_version_line_warns_with_the_reason(self):
        from orbit12_installer.cdc import DeviceStatus
        status = DeviceStatus(orbit12=True, storage_mounted=True, version=None,
                              version_problem="VERSION line not understood (missing engine_api)")
        warnings = device.verify_firmware(self.dev(None, status), self.manifest)
        self.assertTrue(any("missing engine_api" in w for w in warnings))

    def test_not_orbit12_after_flash_fails(self):
        from orbit12_installer.cdc import DeviceStatus
        status = DeviceStatus(orbit12=False, storage_mounted=None, version=None)
        with self.assertRaisesRegex(FlashError, "does not answer as Orbit12"):
            device.verify_firmware(self.dev(None, status), self.manifest)


class BootloaderEntryTests(unittest.TestCase):
    """The 1200-baud touch is judged by its outcome: an exception is inconclusive, not a failure."""

    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM6", files=scenes())
        self.console = RecordingConsole()
        self.hw = hardware(self.world, self.console)

    def test_clean_touch_gives_the_drive_without_any_prompt(self):
        volume = device.enter_bootloader(self.hw, "COM6", timeout_s=5)
        self.assertEqual(volume.path, self.board.volume_path)
        self.assertEqual(self.console.pauses, [])
        self.assertEqual(self.console.warned, [])

    def test_exception_after_the_board_reset_is_not_a_failure_drive_appears_3s_later(self):
        self.board.touch_failure_delay = 3.0
        start = self.clock.monotonic()
        volume = device.enter_bootloader(self.hw, "COM6", timeout_s=30)
        self.assertEqual(volume.path, self.board.volume_path)
        self.assertGreaterEqual(self.clock.monotonic() - start, 3.0)
        self.assertEqual(self.console.pauses, [])  # no BOOT-key instruction
        self.assertFalse(any("BOOT" in w for w in self.console.warned))
        self.assertTrue(any("1200-baud touch" in s for s in self.console.said))

    def test_touch_while_in_maintenance_still_reboots_into_the_bootloader(self):
        self.board.mode = "maintenance"
        volume = device.enter_bootloader(self.hw, "COM6", timeout_s=5)
        self.assertEqual(volume.path, self.board.volume_path)

    def test_port_closed_before_the_touch_and_after_it(self):
        self.board.touch_failure_delay = 1.0
        device.enter_bootloader(self.hw, "COM6", timeout_s=30)
        self.assertTrue(self.world.opened)
        self.assertTrue(all(port.closed for port in self.world.opened))

    def test_port_already_gone_when_opened_still_polls_for_the_drive(self):
        self.board.mode = "gone"
        self.board.deferred_bootsel_at = self.clock.monotonic() + 2.0  # the reset is already under way
        volume = device.enter_bootloader(self.hw, "COM6", timeout_s=30)
        self.assertEqual(volume.path, self.board.volume_path)
        self.assertEqual(self.console.pauses, [])

    def test_drive_never_appears_after_the_touch_falls_back_to_the_boot_key(self):
        self.board.touch_failure_delay = float("inf")

        def hold_boot(message):
            if "BOOT" in message:
                self.board.mode = "normal"
                self.board.touch()  # the user holds BOOT while connecting

        self.console.on_pause = hold_boot
        start = self.clock.monotonic()
        volume = device.enter_bootloader(self.hw, "COM6", timeout_s=30)
        self.assertEqual(volume.path, self.board.volume_path)
        self.assertEqual(len(self.console.pauses), 1)
        self.assertIn("BOOT", self.console.pauses[0])
        self.assertTrue(any("did not appear after the 1200-baud touch" in w for w in self.console.warned))
        self.assertGreaterEqual(self.clock.monotonic() - start, device.TOUCH_SETTLE_S)

    def test_no_drive_at_all_after_the_boot_key_prompt_is_refused(self):
        self.board.touch_failure_delay = float("inf")
        with self.assertRaises(device.NoBootselDrive):
            device.enter_bootloader(self.hw, "COM6", timeout_s=3)
        self.assertEqual(len(self.console.pauses), 1)


class RecentBackupTests(TempDirTest):
    def setUp(self):
        super().setUp()
        self.root = os.path.join(self.tmp, "backups")

    def test_a_backup_from_the_last_hour_for_this_board_is_found(self):
        folder = write_backup(self.root, age_s=600)
        self.assertEqual(device.find_recent_backup(self.root, engine_api=1), folder)

    def test_a_backup_older_than_an_hour_is_not_offered(self):
        write_backup(self.root, age_s=device.RECENT_BACKUP_S + 60)
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))

    def test_a_backup_from_the_future_is_not_offered(self):
        write_backup(self.root, age_s=-600)
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))

    def test_identity_must_name_this_board_and_a_compatible_api(self):
        write_backup(self.root, board="adafruit_feather_rp2040")
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))
        write_backup(self.root, api=2)
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))

    def test_a_backup_without_firmware_identity_is_not_offered(self):
        write_backup(self.root, identity=False)
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))

    def test_a_backup_that_fails_its_hash_check_is_skipped(self):
        folder = write_backup(self.root)
        with open(os.path.join(folder, "Scene01"), "wb") as handle:
            handle.write(b"changed")
        self.assertIsNone(device.find_recent_backup(self.root, engine_api=1))

    def test_the_newest_eligible_backup_wins(self):
        write_backup(self.root, age_s=1200)
        newest = write_backup(self.root, age_s=60)
        self.assertEqual(device.find_recent_backup(self.root, engine_api=1), newest)

    def test_missing_root_is_simply_no_backup(self):
        self.assertIsNone(device.find_recent_backup(os.path.join(self.tmp, "nope"), engine_api=1))


class RenameDeviceTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.world = FakeWorld(self.clock)
        self.board = self.world.add("COM7", files=scenes())
        self.board.mode = "maintenance"
        self.hw = hardware(self.world, RecordingConsole())

    def test_rename_changes_only_the_name_on_the_board(self):
        device.rename_scene(self.hw, "COM7", 1, "ZULU")
        self.assertEqual(self.board.files["scene_names"], b"01,ZULU\n03,BRAVO\n")
        self.assertEqual(self.board.files["Scene01"], scenes()["Scene01"])
        self.assertIn("COM7:RENAME", self.world.log)

    def test_refused_rename_changes_nothing_and_names_the_reason(self):
        with self.assertRaises(cdc.DeviceRefused) as caught:
            device.rename_scene(self.hw, "COM7", 3, "ALFA")
        self.assertEqual(caught.exception.reason, 20)
        self.assertEqual(self.board.files["scene_names"], scenes()["scene_names"])


if __name__ == "__main__":
    unittest.main()
