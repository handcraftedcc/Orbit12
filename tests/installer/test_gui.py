"""The one-window program's logic, driven through the View interface (no tkinter, no display).

A ScriptedView answers every question from a script and records what the window would show. The
same FakeWorld boards, FakeDownloader and maintenance rules as test_flow.py are used, so these
tests check the user-facing flows (install, first install, board not found, cancel, typed
mismatch, releases, port memory, scenes) end to end against the mock hardware.
"""

import json
import os
import threading
import time
import unittest
from unittest import mock

from helpers import (FakeDownloader, FakeWorld, NEW_VERSION, OLD_GIT, OLD_VERSION,
                     TempDirTest, make_uf2, manifest_doc, scenes, sha256, sums_text, write_backup)

from orbit12_installer import cli, flow, gui, guided, github
from orbit12_installer.cdc import ProtocolError
from orbit12_installer.device import Cancelled, FlashError
from orbit12_installer.guided import Bridge, MarshalledView, Outcome, Session, View
from orbit12_installer.package import PackageError

LISTING_URL = "https://api.github.com/repos/handcraftedcc/Orbit12/releases?per_page=100"


class ScriptedView(View):
    """Answers questions from scripts and records what the window would show.

    A question with no scripted answer is recorded in `unexpected` and answered with the safe
    value (no), so a test that asks too many questions fails on its assertion.
    """

    def __init__(self, *, confirm=(), typed=(), actions=(), missing=(), choices=(), folders=(),
                 on_missing=None) -> None:
        self.confirm_answers = list(confirm)
        self.typed_answers = list(typed)
        self.action_answers = list(actions)
        self.missing_answers = list(missing)
        self.choice_answers = list(choices)
        self.folder_answers = list(folders)
        self.on_missing = on_missing
        self.unexpected = []
        self.busy_texts = []
        self.idle_count = 0
        self.outcomes = []
        self.boards = []
        self.releases_seen = []
        self.scenes_seen = []
        self.ports_seen = []
        self.said = []
        self.warned = []
        self.steps = []
        self.typed_prompts = []
        self.confirm_prompts = []

    def busy(self, text): self.busy_texts.append(text)
    def idle(self): self.idle_count += 1
    def step(self, text): self.steps.append(text)
    def say(self, text): self.said.append(text)
    def warn(self, text): self.warned.append(text)
    def ports(self, ports): self.ports_seen.append(list(ports))
    def board(self, board): self.boards.append(board)
    def releases(self, choices): self.releases_seen.append(choices)
    def scenes(self, rows): self.scenes_seen.append(list(rows))
    def outcome(self, outcome): self.outcomes.append(outcome)

    def confirm(self, message, default):
        self.confirm_prompts.append(message)
        if self.confirm_answers:
            return self.confirm_answers.pop(0)
        self.unexpected.append(("confirm", message))
        return False

    def confirm_typed(self, word, message):
        self.typed_prompts.append(word)
        if self.typed_answers:
            return self.typed_answers.pop(0) == word
        self.unexpected.append(("typed", word))
        return False

    def physical_action(self, message):
        if self.action_answers:
            return self.action_answers.pop(0)
        return True

    def board_missing(self, message):
        if self.on_missing is not None:
            self.on_missing()
        if self.missing_answers:
            return self.missing_answers.pop(0)
        self.unexpected.append(("missing", message))
        return False

    def choose(self, prompt, options):
        return self.choice_answers.pop(0) if self.choice_answers else None

    def ask(self, prompt):
        return ""

    def choose_folder(self, title):
        return self.folder_answers.pop(0) if self.folder_answers else None

    @property
    def last(self) -> Outcome:
        return self.outcomes[-1]


def release_json(*releases):
    """releases: (tag, prerelease, with_assets) tuples. Assets are real only for the install tests."""
    return json.dumps([{"tag_name": tag, "name": tag, "prerelease": pre, "draft": False,
                        "published_at": "2026-10-10T00:00:00Z", "body": "notes",
                        "assets": ([{"name": n, "browser_download_url": f"https://github.com/handcraftedcc/Orbit12/releases/download/{tag}/{n}",
                                     "size": 0} for n in ("release-manifest.json", "SHA256SUMS", "Orbit12-0.1.0-pre.1.uf2")]
                                   if with_assets else [])}
                       for tag, pre, with_assets in releases]).encode()


class SessionTest(TempDirTest):
    def setUp(self):
        super().setUp()
        self.image = make_uf2()
        self.manifest = manifest_doc(self.image)
        self.sums = sums_text({"Orbit12-0.1.0-pre.1.uf2": sha256(self.image)})
        self.backups = os.path.join(self.tmp, "backups")
        self.settings_path = os.path.join(self.tmp, "settings.json")
        self.world = FakeWorld()
        self.board = None

    def add_board(self, **kwargs):
        kwargs.setdefault("files", scenes())
        kwargs.setdefault("version", OLD_VERSION)
        kwargs.setdefault("git", OLD_GIT)
        self.board = self.world.add("COM7", **kwargs)
        self.board.reboot_size = len(self.image)
        return self.board

    def downloader(self, listing=None):
        base = "https://github.com/handcraftedcc/Orbit12/releases/download/v0.1.0-pre.1/"
        responses = {
            LISTING_URL: listing if listing is not None else release_json(("v0.1.0-pre.1", True, True)),
            base + "release-manifest.json": self.manifest,
            base + "SHA256SUMS": self.sums,
            base + "Orbit12-0.1.0-pre.1.uf2": self.image,
        }
        return FakeDownloader(responses)

    def release(self):
        return github.parse_releases(release_json(("v0.1.0-pre.1", True, True)))[0]

    def session(self, view, downloader=None, port=None):
        opts = flow.Options(port=port, backup_root=self.backups, bootsel_timeout_s=5,
                            reenum_timeout_s=5, drive_gone_timeout_s=5)
        return Session(view, ports=self.world, opener=self.world, volumes=self.world,
                       clock=self.world.clock, downloader=downloader or self.downloader(), opts=opts,
                       settings_path=self.settings_path)


class InstallTests(SessionTest):
    def test_success_backs_up_then_flashes_and_reports_done(self):
        self.add_board()
        view = ScriptedView(confirm=[True])  # "Put the board in the bootloader and flash now?"
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "done")
        self.assertEqual(self.board.version, NEW_VERSION)
        self.assertIn("COM7:touch", self.world.log)
        self.assertLess(self.world.log.index("COM7:touch"), len(self.world.log))
        self.assertEqual(view.unexpected, [])
        self.assertIn("Backing up scenes and settings", view.steps)
        self.assertIn("Copying the firmware to the board", view.steps)
        self.assertTrue(os.path.isdir(self.backups))
        self.assertTrue(view.busy_texts[0].startswith("Installing"))
        self.assertEqual(view.idle_count, 1)

    def test_first_install_without_backup_needs_typed_flash(self):
        self.add_board(mounted=False, files={})
        self.board.new_mount_state = False
        view = ScriptedView(confirm=[True, False], typed=["FLASH"])  # flash now? / leave unformatted
        self.session(view).install(self.release())
        self.assertEqual(view.typed_prompts, ["FLASH"])
        self.assertEqual(view.last.status, "done")
        self.assertEqual(self.board.version, NEW_VERSION)

    def test_first_install_wrong_typed_word_changes_nothing(self):
        self.add_board(mounted=False, files={})
        view = ScriptedView(typed=["flash"])  # not the exact word
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "cancelled")
        self.assertEqual(view.typed_prompts, ["FLASH"])
        self.assertEqual(self.board.version, OLD_VERSION)
        self.assertNotIn("COM7:touch", self.world.log)

    def test_board_not_found_retry_then_install(self):
        def plug_in():
            if self.board is None:
                self.add_board()
        view = ScriptedView(confirm=[True], missing=[True], on_missing=plug_in)
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "done")
        self.assertEqual(self.board.version, NEW_VERSION)

    def test_board_not_found_cancel_changes_nothing(self):
        view = ScriptedView(missing=[False])
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "cancelled")
        self.assertIn("No MacroPad was found", view.last.text)
        self.assertEqual(self.world.log, [])

    def test_cancel_at_the_bootloader_question_writes_nothing(self):
        self.add_board()
        view = ScriptedView(confirm=[False])
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "cancelled")
        self.assertNotIn("COM7:touch", self.world.log)
        self.assertEqual(self.board.version, OLD_VERSION)

    def test_cancel_on_the_stop_or_hold_step_stops_before_any_write(self):
        self.add_board().info_supported = False  # older firmware: only the hold-a-key step exists
        view = ScriptedView(actions=[False])
        self.session(view).install(self.release())
        self.assertEqual(view.last.status, "cancelled")
        self.assertNotIn("COM7:touch", self.world.log)
        self.assertEqual(self.board.version, OLD_VERSION)

    def test_erase_mismatch_after_flash_formats_nothing(self):
        self.add_board(mounted=False, files={})
        self.board.new_mount_state = False
        view = ScriptedView(confirm=[True, True], typed=["FLASH", "erase"])
        self.session(view).install(self.release())
        self.assertEqual(view.typed_prompts, ["FLASH", "ERASE"])
        self.assertEqual(view.last.status, "cancelled")
        self.assertNotIn("COM7:FORMAT", self.world.log)

    def test_unexpected_error_becomes_a_screen_with_details_only(self):
        self.add_board()
        view = ScriptedView(confirm=[True])
        session = self.session(view)
        with mock.patch.object(flow, "run_flash", side_effect=KeyError("boom")):
            session.install(self.release())
        outcome = view.last
        self.assertEqual(outcome.status, "failed")
        self.assertNotIn("Traceback", outcome.text)
        self.assertIn("Traceback (most recent call last)", outcome.details)
        self.assertIn("KeyError", outcome.details)


class ThreadedTests(SessionTest):
    def test_install_runs_on_a_worker_thread_and_questions_are_answered_on_the_ui_thread(self):
        self.add_board()
        bridge = Bridge()
        view = ScriptedView(confirm=[True])
        session = self.session(MarshalledView(view, bridge))
        ui_threads = []
        original = view.confirm

        def record(message, default):
            ui_threads.append(threading.current_thread())
            return original(message, default)
        view.confirm = record
        worker = threading.Thread(target=session.install, args=(self.release(),))
        worker.start()
        deadline = time.monotonic() + 30
        while worker.is_alive() and time.monotonic() < deadline:
            bridge.pump()
            time.sleep(0.005)
        worker.join(5)
        bridge.pump()
        self.assertFalse(worker.is_alive())
        self.assertEqual(view.last.status, "done")
        self.assertEqual(ui_threads, [threading.main_thread()])


class ReleaseChoiceTests(SessionTest):
    def test_prerelease_only_listing_offers_no_stable_until_the_checkbox_is_ticked(self):
        listing = release_json(("v0.2.0-pre.1", True, False), ("v0.1.0-pre.1", True, False))
        view = ScriptedView()
        self.session(view, self.downloader(listing)).refresh()
        choices = view.releases_seen[-1]
        self.assertIsNone(choices.stable)
        self.assertEqual(choices.newest.tag, "v0.2.0-pre.1")
        self.assertIsNone(guided.pick_release(choices, include_prereleases=False))
        self.assertEqual(guided.pick_release(choices, include_prereleases=True).tag, "v0.2.0-pre.1")

    def test_stable_release_is_the_default_when_one_exists(self):
        listing = release_json(("v0.2.0-pre.1", True, False), ("v0.1.0", False, False))
        view = ScriptedView()
        self.session(view, self.downloader(listing)).refresh()
        choices = view.releases_seen[-1]
        self.assertEqual(guided.pick_release(choices, include_prereleases=False).tag, "v0.1.0")
        self.assertEqual(guided.pick_release(choices, include_prereleases=True).tag, "v0.2.0-pre.1")

    def test_unreachable_github_still_shows_the_board(self):
        self.add_board()
        view = ScriptedView()
        session = self.session(view, FakeDownloader({}))  # listing request fails
        session.refresh()
        self.assertEqual(view.boards[-1].kind, "orbit12")
        self.assertTrue(view.releases_seen[-1].error)
        self.assertIsNone(view.releases_seen[-1].newest)


class BoardDetectionTests(SessionTest):
    def test_orbit12_board_shows_firmware_and_saved_scenes(self):
        self.add_board()
        view = ScriptedView()
        self.session(view).refresh()
        board = view.boards[-1]
        self.assertEqual(board.kind, "orbit12")
        self.assertEqual(board.port, "COM7")
        self.assertIn(OLD_VERSION, board.firmware)
        self.assertIn("saved scene", board.storage)

    def test_empty_bench_says_not_found(self):
        view = ScriptedView()
        self.session(view).refresh()
        self.assertEqual(view.boards[-1].kind, "none")
        self.assertFalse(view.boards[-1].present)

    def test_two_boards_are_refused_in_plain_words(self):
        self.add_board()
        self.world.add("COM8", files=scenes())
        view = ScriptedView()
        self.session(view).refresh()
        self.assertEqual(view.boards[-1].kind, "error")
        self.assertIn("Unplug all but one", view.boards[-1].firmware)


class PortMemoryTests(SessionTest):
    def test_chosen_port_is_remembered_and_automatic_forgets_it(self):
        view = ScriptedView()
        session = self.session(view)
        session.set_port("COM9")
        self.assertEqual(guided.load_settings(self.settings_path), {"port": "COM9"})
        session.set_port(None)
        self.assertEqual(guided.load_settings(self.settings_path), {})

    def test_port_list_is_shown_with_descriptions(self):
        self.world.extra_ports = []
        self.add_board()
        view = ScriptedView()
        self.session(view).refresh_ports()
        self.assertEqual([p.device for p in view.ports_seen[-1]], ["COM7"])

    def test_missing_settings_file_is_not_an_error(self):
        self.assertEqual(guided.load_settings(os.path.join(self.tmp, "absent.json")), {})
        guided.save_settings(os.path.join(self.tmp, "no-dir", "x.json"), {"port": "COM1"})  # must not raise


class ScenesTests(SessionTest):
    def setUp(self):
        super().setUp()
        self.add_board(files=scenes())
        self.scene_names = sorted(n for n in scenes() if n.startswith("Scene"))

    def test_listing_marks_saved_slots(self):
        view = ScriptedView()
        self.session(view).load_scenes()
        rows = view.scenes_seen[-1]
        self.assertEqual(len(rows), 16)
        saved = {row.slot for row in rows if row.saved}
        self.assertEqual(saved, set(self.scene_names))

    def test_backup_one_scene_writes_only_that_scene(self):
        view = ScriptedView()
        self.session(view).backup_scenes("Scene01")
        self.assertEqual(view.last.status, "done")
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        self.assertEqual(sorted(f for f in os.listdir(folder) if f.startswith("Scene")), ["Scene01"])

    def test_backup_all_writes_every_file(self):
        view = ScriptedView()
        self.session(view).backup_scenes(None)
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        self.assertEqual(sorted(f for f in os.listdir(folder) if f.startswith("Scene")), self.scene_names)

    def test_restore_one_scene_replaces_only_that_scene_after_yes(self):
        original = dict(self.board.files)
        view = ScriptedView()
        self.session(view).backup_scenes(None)
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        self.board.files["Scene01"] = b"CHANGED 1\n"
        self.board.files["Scene03"] = b"CHANGED 3\n"
        view = ScriptedView(folders=[folder], choices=[0], confirm=[True])
        self.session(view).restore_scenes(one=True)
        self.assertEqual(view.last.status, "done")
        self.assertEqual(self.board.files["Scene01"], original["Scene01"])
        self.assertEqual(self.board.files["Scene03"], b"CHANGED 3\n")

    def test_restore_one_scene_declined_changes_nothing(self):
        view = ScriptedView()
        self.session(view).backup_scenes(None)
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        self.board.files["Scene01"] = b"CHANGED 1\n"
        view = ScriptedView(folders=[folder], choices=[0], confirm=[False])
        self.session(view).restore_scenes(one=True)
        self.assertEqual(view.last.status, "cancelled")
        self.assertEqual(self.board.files["Scene01"], b"CHANGED 1\n")

    def test_restore_all_needs_the_typed_word_and_mismatch_changes_nothing(self):
        view = ScriptedView()
        self.session(view).backup_scenes(None)
        folder = os.path.join(self.backups, os.listdir(self.backups)[0])
        self.board.files["Scene01"] = b"CHANGED 1\n"
        view = ScriptedView(folders=[folder], typed=["erase"])
        self.session(view).restore_scenes(one=False)
        self.assertEqual(view.typed_prompts, ["ERASE"])
        self.assertEqual(view.last.status, "cancelled")
        self.assertEqual(self.board.files["Scene01"], b"CHANGED 1\n")

    def test_restore_refuses_a_damaged_backup_before_touching_the_board(self):
        folder = write_backup(self.backups, files=scenes())
        with open(os.path.join(folder, "Scene01"), "wb") as handle:
            handle.write(b"tampered")
        view = ScriptedView(folders=[folder])
        self.session(view).restore_scenes(one=False)
        self.assertEqual(view.last.status, "failed")
        self.assertFalse(any(entry.startswith("COM7:PUT") for entry in self.world.log))


class ErrorTextTests(unittest.TestCase):
    def test_release_problem_says_nothing_was_written(self):
        outcome = guided.describe_error(PackageError("bad sums"), [])
        self.assertEqual(outcome.status, "failed")
        self.assertIn("Nothing was written", outcome.next_step)

    def test_protocol_errors_have_plain_next_steps(self):
        outcome = guided.describe_error(ProtocolError("bad frame"), ["Log line"])
        self.assertIn("press Retry", outcome.next_step)
        self.assertIn("Log line", outcome.details)
        self.assertNotIn("Traceback", outcome.details)

    def test_cancel_is_not_a_failure(self):
        self.assertEqual(guided.describe_error(Cancelled("no"), []).status, "cancelled")

    def test_old_console_wording_is_translated_for_the_window(self):
        outcome = guided.describe_error(FlashError("run `orbit12-installer format` later"), [])
        self.assertNotIn("orbit12-installer format", outcome.text)


class GuiModuleTests(unittest.TestCase):
    def test_no_display_on_linux_means_no_window(self):
        with mock.patch.object(gui.sys, "platform", "linux"), mock.patch.dict(os.environ, {}, clear=True):
            self.assertFalse(gui.available())

    def test_run_app_returns_none_without_a_window(self):
        with mock.patch.object(gui, "available", return_value=False):
            self.assertIsNone(gui.run_app())

    def test_no_arguments_open_the_window_and_fall_back_to_the_wizard_when_it_cannot(self):
        with mock.patch.object(gui, "run_app", return_value=0) as run_app, \
                mock.patch.object(cli, "run", return_value=0) as run:
            self.assertEqual(cli.main([]), 0)
            run_app.assert_called_once()
            run.assert_not_called()
        with mock.patch.object(gui, "run_app", return_value=None), \
                mock.patch.object(cli, "run", return_value=3) as run:
            self.assertEqual(cli.main([]), 3)
            run.assert_called_once_with([])

    def test_arguments_and_cli_flag_never_open_the_window(self):
        with mock.patch.object(gui, "run_app") as run_app, mock.patch.object(cli, "run", return_value=0):
            cli.main(["releases"])
            cli.main(["--cli"])
            run_app.assert_not_called()

    def test_cli_flag_is_listed_in_help(self):
        self.assertIn("--cli", cli.build_parser().format_help())

    def test_window_is_built_only_when_tk_actually_opens(self):
        if os.environ.get("ORBIT12_GUI_SMOKE") != "1":
            self.skipTest("set ORBIT12_GUI_SMOKE=1 on a machine with a display to build the real window")
        import tkinter
        try:
            root = tkinter.Tk()
        except tkinter.TclError:
            self.skipTest("no display")
        try:
            window = gui.InstallerWindow(root)
            self.assertEqual(window.notebook.index("end"), 3)
        finally:
            root.destroy()


class BuildFlagTests(unittest.TestCase):
    def test_windows_and_macos_builds_have_no_console_window(self):
        import build_exe
        for platform, flag in (("win32", "--windowed"), ("darwin", "--windowed"), ("linux", "--console")):
            with mock.patch.object(build_exe.sys, "platform", platform):
                self.assertEqual(build_exe.window_flag(), flag)
                self.assertIn(flag, build_exe.build_command("orbit12-installer-test"))


if __name__ == "__main__":
    unittest.main()
