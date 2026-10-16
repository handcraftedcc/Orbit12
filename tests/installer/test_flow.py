"""End-to-end install, repair and maintenance flows against the fake board (no hardware)."""

import json
import os
import unittest

from helpers import (write_backup, NEW_GIT, NEW_VERSION, OLD_GIT, OLD_VERSION, FakeDownloader, FakeWorld,
                     RecordingConsole, TempDirTest, hardware, make_uf2, manifest_doc, scenes, sha256,
                     sums_text)

from orbit12_installer import device, flow, github, package
from orbit12_installer.interfaces import Volume
from orbit12_installer.device import Cancelled, FlashError
from orbit12_installer.package import PackageError


def hold_on_prompt(board):
    """Simulates the user holding a key at power-up when the installer asks for it."""
    def on_pause(message):
        if "hold any key" in message:
            board.mode = "maintenance"
    return on_pause


class FlowTest(TempDirTest):
    def setUp(self):
        super().setUp()
        self.image = make_uf2()
        self.manifest_bytes = manifest_doc(self.image)
        self.uf2_path = os.path.join(self.tmp, "Orbit12-0.1.0-pre.1.uf2")
        self.manifest_path = os.path.join(self.tmp, "release-manifest.json")
        self.sums_path = os.path.join(self.tmp, "SHA256SUMS")
        self.backups = os.path.join(self.tmp, "backups")
        with open(self.uf2_path, "wb") as handle:
            handle.write(self.image)
        with open(self.manifest_path, "wb") as handle:
            handle.write(self.manifest_bytes)
        with open(self.sums_path, "wb") as handle:
            handle.write(sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(self.image)}))
        self.world = FakeWorld()
        self.board = self.world.add("COM7", files=scenes(), version=OLD_VERSION, git=OLD_GIT)
        self.board.reboot_size = len(self.image)
        self.console = RecordingConsole(on_pause=hold_on_prompt(self.board))
        self.hw = hardware(self.world, self.console)
        self.opts = flow.Options(uf2=self.uf2_path, manifest=self.manifest_path, sums=self.sums_path,
                                 backup_root=self.backups, bootsel_timeout_s=5,
                                 reenum_timeout_s=5, drive_gone_timeout_s=5)

    def run_flash_offline(self):
        package_ = flow.load_local_package(self.opts)
        return flow.run_flash(self.hw, package_, self.opts, interactive=False)

    def index(self, entry):
        return self.world.log.index(entry)


class InstallFlowTests(FlowTest):
    def test_happy_path_backs_up_before_bootsel_and_keeps_scenes(self):
        summary = self.run_flash_offline()
        self.assertIn(NEW_VERSION, summary)
        self.assertEqual(self.board.version, NEW_VERSION)
        self.assertEqual(self.board.git, NEW_GIT)
        # Backup first: scenes are read before the 1200-baud touch and before any drive appears.
        self.assertLess(self.index("COM7:GET:Scene01"), self.index("COM7:touch"))
        self.assertLess(self.index("COM7:ENTER"), self.index("COM7:GET:Scene01"))
        self.assertLess(self.index("COM7:EXIT"), self.index("COM7:touch"))
        self.assertLess(self.index("COM7:touch"), self.index("COM7:reboot"))
        self.assertNotIn("COM7:FORMAT", self.world.log)
        self.assertEqual(self.board.files, scenes())  # the partition is not touched by the image
        backups = os.listdir(self.backups)
        self.assertEqual(len(backups), 1)
        self.assertTrue(os.path.exists(os.path.join(self.backups, backups[0], "backup.json")))

    def test_verified_image_is_kept_beside_the_backup_for_manual_recovery(self):
        self.run_flash_offline()
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        with open(os.path.join(folder, "Orbit12-0.1.0-pre.1.uf2"), "rb") as handle:
            self.assertEqual(handle.read(), self.image)
        # The extra file does not confuse restore: only the listed entries are read.
        device.load_backup(folder)

    def test_warns_when_the_firmware_has_no_version_line_yet(self):
        self.board.info_supported = False  # older firmware: no INFO, and no VERSION line either
        self.board.next_version = None
        summary = self.run_flash_offline()
        self.assertIn("Orbit12", summary)
        self.assertTrue(any("VERSION" in w for w in self.console.warned))

    def test_wrong_version_after_reboot_fails_verification(self):
        self.board.next_version = "0.0.5"
        with self.assertRaisesRegex(FlashError, "expected 0.1.0-pre.1"):
            self.run_flash_offline()

    def test_board_that_does_not_reenumerate_gives_recovery_steps(self):
        self.board.reenumerates = False
        self.opts.reenum_timeout_s = 3
        with self.assertRaisesRegex(FlashError, "did not come back.*Recovery"):
            self.run_flash_offline()
        self.assertIn("COM7:reboot", self.world.log)

    def test_no_backup_needs_the_typed_word_flash_and_refuses_otherwise(self):
        self.board.info_supported = False  # no host entry: only the key-hold step is left
        self.console.on_pause = None  # the user never gets the board into maintenance
        self.console.yes = [False]  # do not retry the power-cycle
        self.console.typed = ["no"]
        with self.assertRaises(Cancelled):
            self.run_flash_offline()
        self.assertNotIn("COM7:touch", self.world.log)
        self.assertEqual(self.console.typed_prompts, ["FLASH"])

    def test_no_backup_proceeds_only_after_typing_flash(self):
        self.console.on_pause = None
        self.console.yes = [False]
        self.console.typed = ["FLASH"]
        self.run_flash_offline()
        self.assertIn("COM7:touch", self.world.log)
        self.assertEqual(self.board.version, NEW_VERSION)

    def test_declining_the_bootloader_step_writes_nothing(self):
        self.console.yes = [False]  # "Put the board in the bootloader and flash now?"
        package_ = flow.load_local_package(self.opts)
        with self.assertRaises(Cancelled):
            flow.run_flash(self.hw, package_, self.opts, interactive=True)
        self.assertNotIn("COM7:touch", self.world.log)
        self.assertEqual(self.board.version, OLD_VERSION)


class UnmountedPartitionTests(FlowTest):
    def setUp(self):
        super().setUp()
        self.board.mounted = False
        self.board.files = {}
        self.board.new_mount_state = False  # the new firmware still reports STORAGE mounted=0

    def test_unmounted_after_flash_formats_after_typed_erase_and_restores_backup(self):
        self.board.mounted = True  # a backup exists only when the partition was readable
        self.board.files = scenes()
        self.console.yes = [True]  # "format now?" (the bootloader prompt is skipped offline)
        self.console.typed = ["ERASE"]
        self.run_flash_offline()
        self.assertTrue(self.board.mounted)
        self.assertEqual(self.board.files, scenes())
        self.assertLess(self.index("COM7:FORMAT"), self.index("COM7:PUT:Scene01"))
        self.assertIn("ERASE", self.console.typed_prompts)

    def test_unmounted_without_backup_formats_only_with_typed_erase(self):
        self.console.yes = [True]
        self.console.typed = ["FLASH", "ERASE"]
        self.run_flash_offline()
        self.assertTrue(self.board.mounted)
        self.assertEqual(self.board.files, {})
        self.assertEqual(self.console.typed_prompts, ["FLASH", "ERASE"])

    def test_declining_format_leaves_the_board_running_with_a_warning(self):
        self.console.yes = [False]
        self.console.typed = ["FLASH"]
        self.run_flash_offline()
        self.assertFalse(self.board.mounted)
        self.assertNotIn("COM7:FORMAT", self.world.log)
        self.assertTrue(any("Left unformatted" in w for w in self.console.warned))

    def test_format_refused_by_typing_something_else_changes_nothing(self):
        self.console.yes = [True]
        self.console.typed = ["FLASH", "erase"]
        with self.assertRaises(Cancelled):
            self.run_flash_offline()
        self.assertNotIn("COM7:FORMAT", self.world.log)
        self.assertFalse(self.board.mounted)


class MaintenanceCommandTests(FlowTest):
    def test_format_command_needs_erase_and_reboots_to_a_mounted_partition(self):
        self.board.mounted = False
        self.board.files = {}
        self.console.typed = ["ERASE"]
        result = flow.format_only(self.hw, self.opts)
        self.assertEqual(result, "Partition formatted and mounted.")
        self.assertTrue(self.board.mounted)
        self.assertIn("COM7:FORMAT", self.world.log)

    def test_restore_command_writes_the_backup_and_leaves_maintenance(self):
        folder = device.backup_device(self.hw, self._maintenance(), self.backups)
        self.board.files = {}
        self.board.mode = "normal"
        self.console.typed = ["ERASE"]
        message = flow.restore_only(self.hw, self.opts, folder)
        self.assertIn("scene_names", message)
        self.assertEqual(self.board.files["Scene01"], scenes()["Scene01"])
        self.assertEqual(self.board.mode, "normal")

    def test_restore_needs_the_typed_word_erase_before_any_write(self):
        folder = device.backup_device(self.hw, self._maintenance(), self.backups)
        self.board.files = {}
        self.board.mode = "normal"
        self.console.typed = ["yes"]
        with self.assertRaises(Cancelled):
            flow.restore_only(self.hw, self.opts, folder)
        self.assertFalse(any(":PUT:" in entry for entry in self.world.log))
        self.assertEqual(self.board.mode, "normal")

    def test_restore_refuses_an_incomplete_folder_before_touching_the_board(self):
        empty = os.path.join(self.tmp, "not-a-backup")
        os.makedirs(empty)
        with self.assertRaisesRegex(FlashError, "not a complete backup"):
            flow.restore_only(self.hw, self.opts, empty)
        self.assertEqual(self.world.log, [])

    def test_backup_command_reports_the_folder(self):
        self.board.mode = "normal"
        message = flow.backup_only(self.hw, self.opts)
        self.assertIn(self.backups, message)

    def test_info_reports_storage_and_version(self):
        text = flow.info(self.hw, self.opts)
        self.assertIn("mounted=1", text)
        self.assertIn(OLD_VERSION, text)

    def test_info_without_a_board_says_so(self):
        self.board.mode = "bootsel"
        self.assertIn("No Orbit12", flow.info(self.hw, self.opts))

    def _maintenance(self):
        self.board.mode = "maintenance"
        return "COM7"


class OfflineModeTests(FlowTest):
    def test_real_clean_build_shaped_image_loads_offline(self):
        # 1072 blocks of 256 bytes from 0x10000000 to 0x10043000, filesystem 1 MiB at 0x106ff000.
        image = make_uf2(addresses=[0x10000000 + 256 * i for i in range(1072)])
        self.assertEqual(len(image), 548864)
        with open(self.uf2_path, "wb") as handle:
            handle.write(image)
        with open(self.manifest_path, "wb") as handle:
            handle.write(manifest_doc(image))
        with open(self.sums_path, "wb") as handle:
            handle.write(sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(image)}))
        package_ = flow.load_local_package(self.opts)
        self.assertEqual(package_.manifest.image.blocks, 1072)
        self.assertEqual(package_.manifest.image.address_end, 0x10043000)

    def test_local_package_verifies_before_anything_is_written(self):
        package_ = flow.load_local_package(self.opts)
        self.assertEqual(package_.manifest.version, NEW_VERSION)
        self.assertEqual(package_.source, "local files (Orbit12-0.1.0-pre.1.uf2)")

    def test_local_package_refuses_a_tampered_image(self):
        tampered = bytearray(self.image)
        tampered[300] ^= 0xFF
        with open(self.uf2_path, "wb") as handle:
            handle.write(bytes(tampered))
        with self.assertRaises(PackageError):
            flow.load_local_package(self.opts)

    def test_local_package_refuses_a_tampered_sums_file(self):
        with open(self.sums_path, "wb") as handle:
            handle.write(sums_text({"Orbit12-0.1.0-pre.1.uf2": "0" * 64}))
        with self.assertRaises(PackageError):
            flow.load_local_package(self.opts)

    def test_local_package_refuses_wrong_board_and_wrong_family(self):
        with open(self.manifest_path, "wb") as handle:
            handle.write(manifest_doc(self.image, board="adafruit_feather_rp2040"))
        with self.assertRaisesRegex(PackageError, "board"):
            flow.load_local_package(self.opts)
        bad = make_uf2(family=0x1234)
        with open(self.uf2_path, "wb") as handle:
            handle.write(bad)
        with open(self.manifest_path, "wb") as handle:
            handle.write(manifest_doc(bad))
        with open(self.sums_path, "wb") as handle:
            handle.write(sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(bad)}))
        with self.assertRaisesRegex(PackageError, "UF2 check failed"):
            flow.load_local_package(self.opts)

    def test_offline_needs_both_files(self):
        self.opts.manifest = None
        with self.assertRaisesRegex(PackageError, "need both"):
            flow.load_local_package(self.opts)

    def test_offline_oversize_manifest_is_refused(self):
        with open(self.manifest_path, "wb") as handle:
            handle.write(b" " * (package.MAX_MANIFEST_BYTES + 10))
        with self.assertRaisesRegex(PackageError, "larger"):
            flow.load_local_package(self.opts)


class CircuitPythonTests(FlowTest):
    """A CircuitPython board: CIRCUITPY is copied read-only, the CDC port is reset, nothing is restored."""

    def setUp(self):
        super().setUp()
        self.board.legacy = True
        self.board.mounted = False
        self.board.files = {}
        self.board.new_mount_state = False  # the Orbit12 firmware starts unformatted (FAT on the partition)
        self.circuitpy = os.path.join(self.tmp, "CIRCUITPY")
        scenes_dir = os.path.join(self.circuitpy, "userdata", "Orbit12Orion", "scenes")
        os.makedirs(scenes_dir)
        with open(os.path.join(scenes_dir, "Scene01"), "wb") as handle:
            handle.write(b"SCN1 from circuitpython\n")
        self.world.extra_volumes.append(Volume(self.circuitpy, "", frozenset({"boot_out.txt", "code.py"})))

    def test_copies_userdata_resets_over_usb_and_formats_without_restoring(self):
        self.console.yes = [True, True]  # reset over USB, then "format now?"
        self.console.typed = ["ERASE"]
        self.run_flash_offline()
        self.assertIn("COM7:touch", self.world.log)
        copies = [d for d in os.listdir(self.backups) if d.startswith("circuitpy-userdata")]
        self.assertEqual(len(copies), 1)
        self.assertTrue(os.path.exists(os.path.join(self.backups, copies[0], "userdata", "Orbit12Orion",
                                                    "scenes", "Scene01")))
        self.assertFalse(any(":PUT:" in entry for entry in self.world.log))
        self.assertIn("COM7:FORMAT", self.world.log)
        self.assertTrue(self.board.mounted)
        self.assertEqual(self.board.version, NEW_VERSION)

    def test_declining_the_usb_reset_asks_for_boot_instead(self):
        self.console.yes = [False, True]  # decline the USB reset, then "format now?"
        self.console.typed = ["ERASE"]
        # Holding BOOT while connecting makes the bootloader drive appear; no CDC touch is sent.
        hold = hold_on_prompt(self.board)

        def on_pause(message):
            if "BOOT" in message:
                self.board.touch()
            else:
                hold(message)

        self.console.on_pause = on_pause
        self.run_flash_offline()
        self.assertTrue(any("BOOT" in p for p in self.console.pauses))
        self.assertEqual(self.board.version, NEW_VERSION)
        self.assertTrue(any("BOOT" in p for p in self.console.pauses))


class RestoreFailureTests(FlowTest):
    def test_restore_failure_after_format_reports_the_backup_and_leaves_maintenance(self):
        self.board.mounted = False
        self.board.files = {}
        folder = device.backup_device(self.hw, self._hold(), self.backups)
        self.board.mode = "normal"
        self.board.mounted = True
        self.board.files = scenes()
        self.board.new_mount_state = False
        self.board.refuse_puts = True  # the restore is refused after the format
        self.console.yes = [True]  # "format now?"; the bootloader prompt is skipped offline
        self.console.typed = ["ERASE"]
        with self.assertRaisesRegex(FlashError, "restore stopped.*restore --from"):
            self.run_flash_offline()
        self.assertIn("COM7:FORMAT", self.world.log)
        self.assertTrue(any(":PUT:" in entry for entry in self.world.log))
        self.assertEqual(self.board.mode, "normal")  # Q was sent: the board is back in performance mode

    def _hold(self):
        self.board.mode = "maintenance"
        return "COM7"


class OnlineSelectionTests(FlowTest):
    """Release assets come from a fake downloader that enforces the same limits as the real one."""

    def release_for(self, *, tag="v0.1.0-pre.1", repo_asset_names=None, repo="handcraftedcc/Orbit12",
                    manifest_overrides=None, image=None):
        image = image or self.image
        overrides = dict(manifest_overrides or {})
        overrides.setdefault("repo", repo)
        manifest = manifest_doc(image, **overrides)
        self.manifest_bytes = manifest
        self.sums = sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(image)})
        names = repo_asset_names or ["release-manifest.json", "SHA256SUMS", "Orbit12-0.1.0-pre.1.uf2"]
        assets = [{"name": n, "browser_download_url": f"https://github.com/{repo}/releases/download/{tag}/{n}",
                   "size": len(manifest) if n == "release-manifest.json" else 0} for n in names]
        raw = [{"tag_name": tag, "name": tag, "prerelease": True, "draft": False,
                "published_at": "2026-10-10T00:00:00Z", "body": "notes", "assets": assets}]
        releases = github.parse_releases(json.dumps(raw).encode())
        self.downloader = FakeDownloader({
            f"https://github.com/{repo}/releases/download/{tag}/release-manifest.json": manifest,
            f"https://github.com/{repo}/releases/download/{tag}/SHA256SUMS": self.sums,
            f"https://github.com/{repo}/releases/download/{tag}/Orbit12-0.1.0-pre.1.uf2": image,
        })
        return releases[0]

    def test_download_verifies_manifest_sums_and_image(self):
        release = self.release_for()
        package_ = flow.download_package(self.hw, self.downloader, release, self.opts)
        self.assertEqual(package_.manifest.version, NEW_VERSION)
        self.assertEqual(package_.image, self.image)

    def test_missing_manifest_asset_is_refused_without_fallback(self):
        release = self.release_for(repo_asset_names=["SHA256SUMS", "Orbit12-0.1.0-pre.1.uf2"])
        from orbit12_installer.github import ReleaseError
        with self.assertRaisesRegex(ReleaseError, "lacks"):
            flow.download_package(self.hw, self.downloader, release, self.opts)

    def test_tag_that_disagrees_with_the_manifest_version_is_refused(self):
        release = self.release_for(tag="v0.9.9-pre.1")
        with self.assertRaisesRegex(PackageError, "does not match manifest version"):
            flow.download_package(self.hw, self.downloader, release, self.opts)

    def test_tampered_sha256sums_on_the_server_is_refused(self):
        release = self.release_for()
        self.downloader.responses["https://github.com/handcraftedcc/Orbit12/releases/download/"
                                  "v0.1.0-pre.1/SHA256SUMS"] = sums_text({"Orbit12-0.1.0-pre.1.uf2": "f" * 64})
        with self.assertRaises(PackageError):
            flow.download_package(self.hw, self.downloader, release, self.opts)

    def test_bad_image_bytes_from_the_server_are_refused(self):
        release = self.release_for()
        bad = bytearray(self.image)
        bad[10] ^= 1
        self.downloader.responses["https://github.com/handcraftedcc/Orbit12/releases/download/"
                                  "v0.1.0-pre.1/Orbit12-0.1.0-pre.1.uf2"] = bytes(bad)
        with self.assertRaises(PackageError):
            flow.download_package(self.hw, self.downloader, release, self.opts)

    def test_non_default_repo_requires_typing_its_name_even_with_yes(self):
        repo = "someone/fork"
        release = self.release_for(repo=repo)
        self.opts.repo = repo
        self.console.assume_yes = True
        self.console.typed = ["yes"]
        with self.assertRaises(Cancelled):
            flow.download_package(self.hw, self.downloader, release, self.opts)
        self.assertEqual(self.downloader.requests, [])
        self.assertIn(repo, self.console.typed_prompts)

    def test_non_default_repo_proceeds_after_the_exact_name_is_typed(self):
        repo = "someone/fork"
        release = self.release_for(repo=repo)
        self.opts.repo = repo
        self.console.typed = [repo]
        package_ = flow.download_package(self.hw, self.downloader, release, self.opts)
        self.assertEqual(package_.manifest.repo, repo)
        self.assertTrue(any("NOT the official" in w for w in self.console.warned))

    def test_manifest_naming_another_repo_is_refused(self):
        # The fork's release carries the official repo name in its manifest.
        release = self.release_for(repo="someone/fork", manifest_overrides={"repo": "handcraftedcc/Orbit12"})
        self.opts.repo = "someone/fork"
        self.console.typed = ["someone/fork"]
        with self.assertRaisesRegex(PackageError, "does not match the requested repo"):
            flow.download_package(self.hw, self.downloader, release, self.opts)

    def test_repository_comparison_ignores_case(self):
        release = self.release_for(repo="HandcraftedCC/Orbit12")
        self.opts.repo = "HandcraftedCC/Orbit12"
        flow.download_package(self.hw, self.downloader, release, self.opts)
        self.assertFalse(self.console.typed_prompts)
        self.assertFalse(any("NOT the official" in w for w in self.console.warned))

    def test_token_is_passed_to_the_downloader_only_when_set(self):
        release = self.release_for()
        os.environ.pop("GITHUB_TOKEN", None)
        flow.download_package(self.hw, self.downloader, release, self.opts)
        self.assertTrue(all(token is None for _, token in self.downloader.requests))


class AlreadyInBootloaderTests(FlowTest):
    """The board starts in its bootloader: no Orbit12 USB port, one RPI-RP2 drive."""

    def setUp(self):
        super().setUp()
        self.board.touch()  # the board is already in its bootloader (mode bootsel, drive present)
        self.assertIsNone(device.find_orbit12(self.hw))

    def test_recent_verified_backup_is_offered_and_used_without_typed_flash(self):
        folder = write_backup(self.backups, age_s=600)
        self.console.yes = [True]  # "use this backup?"
        summary = self.run_flash_offline()
        self.assertIn("installed", summary)
        self.assertEqual(self.console.typed_prompts, [])
        self.assertEqual(self.board.version, NEW_VERSION)
        self.assertTrue(os.path.exists(os.path.join(folder, "Orbit12-0.1.0-pre.1.uf2")))

    def test_declining_the_recent_backup_needs_the_typed_word_flash(self):
        write_backup(self.backups, age_s=600)
        self.console.yes = [False]
        self.console.typed = ["FLASH"]
        self.run_flash_offline()
        self.assertEqual(self.console.typed_prompts, ["FLASH"])
        self.assertEqual(self.board.version, NEW_VERSION)

    def test_declining_without_typing_flash_changes_nothing(self):
        write_backup(self.backups, age_s=600)
        self.console.yes = [False]
        self.console.typed = ["flash"]
        with self.assertRaises(Cancelled):
            self.run_flash_offline()
        self.assertEqual(self.board.version, OLD_VERSION)
        self.assertEqual(self.board.mode, "bootsel")

    def test_an_old_backup_is_not_offered_and_typed_flash_is_asked(self):
        write_backup(self.backups, age_s=device.RECENT_BACKUP_S + 600)
        self.console.typed = ["FLASH"]
        self.run_flash_offline()
        self.assertEqual(self.console.typed_prompts, ["FLASH"])
        self.assertEqual(self.console.yes, [])  # never asked about the backup

    def test_a_backup_without_firmware_identity_is_not_offered(self):
        write_backup(self.backups, age_s=60, identity=False)
        self.console.typed = ["FLASH"]
        self.run_flash_offline()
        self.assertEqual(self.console.typed_prompts, ["FLASH"])

    def test_two_rpi_rp2_drives_are_refused(self):
        other = self.world.add("COM8")
        other.touch()
        self.console.typed = ["FLASH"]
        with self.assertRaisesRegex(FlashError, "several RPI-RP2"):
            self.run_flash_offline()
        self.assertEqual(self.board.version, OLD_VERSION)


if __name__ == "__main__":
    unittest.main()
