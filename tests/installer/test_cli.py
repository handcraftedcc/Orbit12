"""Command line, wizard, terminal console and build script wiring (no hardware, no network)."""

import io
import os
import subprocess
import sys
import unittest

from helpers import (FakeDownloader, FakeWorld, INSTALLER, RecordingConsole, TempDirTest, hardware,
                     make_uf2, manifest_doc, scenes)

from orbit12_installer import cli
from orbit12_installer.console import TerminalConsole
from orbit12_installer.device import Cancelled


def run_cli(argv, world, console, downloader=None):
    return cli.run(argv, console=console, hardware=hardware(world, console),
                   downloader=downloader or FakeDownloader({}))


class ParserTests(unittest.TestCase):
    def test_no_arguments_gives_the_wizard(self):
        args = cli.build_parser().parse_args([])
        self.assertIsNone(args.command)
        self.assertEqual(args.repo, "handcraftedcc/Orbit12")

    def test_options_before_or_after_the_subcommand_both_apply(self):
        parser = cli.build_parser()
        before = parser.parse_args(["--repo", "fork/Orbit12", "releases"])
        after = parser.parse_args(["releases", "--repo", "fork/Orbit12"])
        self.assertEqual(before.repo, "fork/Orbit12")
        self.assertEqual(after.repo, "fork/Orbit12")
        mixed = parser.parse_args(["--yes", "install", "--tag", "v0.1.0"])
        self.assertTrue(mixed.yes)
        self.assertEqual(mixed.tag, "v0.1.0")

    def test_subcommand_defaults_do_not_overwrite_earlier_options(self):
        args = cli.build_parser().parse_args(["--include-prereleases", "releases"])
        self.assertTrue(args.include_prereleases)

    def test_all_commands_exist(self):
        parser = cli.build_parser()
        for command in ("install", "flash", "releases", "info", "backup", "format"):
            self.assertEqual(parser.parse_args([command]).command, command)
        self.assertEqual(parser.parse_args(["restore", "--from", "x"]).command, "restore")
        with self.assertRaises(SystemExit):
            parser.parse_args(["restore"])  # --from is required


class CommandTests(TempDirTest):
    def test_info_prints_storage_and_version(self):
        world = FakeWorld()
        world.add("COM7")
        console = RecordingConsole()
        self.assertEqual(run_cli(["info"], world, console), cli.EXIT_OK)
        self.assertIn("mounted=1", console.said[-1])

    def test_failures_exit_with_one_and_say_why(self):
        world = FakeWorld()
        console = RecordingConsole()
        self.assertEqual(run_cli(["backup"], world, console), cli.EXIT_FAILED)
        self.assertTrue(any("no Orbit12" in w for w in console.warned))

    def test_format_without_maintenance_fails_and_erases_nothing(self):
        world = FakeWorld()
        board = world.add("COM7", files=scenes())
        board.info_supported = False  # older firmware: no host entry, so only the key-hold step exists
        console = RecordingConsole(typed=["ERASE"], yes=[False, False, False])
        code = run_cli(["format"], world, console)
        self.assertEqual(code, cli.EXIT_FAILED)
        self.assertFalse(any(":FORMAT" in entry for entry in world.log))
        self.assertEqual(console.typed_prompts, [])  # the typed word is never asked for

    def test_declined_typed_erase_exits_with_three(self):
        world = FakeWorld()
        board = world.add("COM7", files=scenes())
        console = RecordingConsole(typed=["nope"], on_pause=lambda msg: setattr(board, "mode", "maintenance"))
        code = run_cli(["format"], world, console)
        self.assertEqual(code, cli.EXIT_CANCELLED)
        self.assertFalse(any(":FORMAT" in entry for entry in world.log))
        self.assertEqual(board.files, scenes())

    def test_wizard_quit_changes_nothing(self):
        world = FakeWorld()
        world.add("COM7")
        console = RecordingConsole(choices=[5])
        self.assertEqual(run_cli([], world, console), cli.EXIT_OK)
        self.assertNotIn("COM7:touch", world.log)

    def test_wizard_restore_asks_for_the_folder(self):
        world = FakeWorld()
        world.add("COM7")
        console = RecordingConsole(choices=[2], asks=[os.path.join(self.tmp, "missing")])
        self.assertEqual(run_cli([], world, console), cli.EXIT_FAILED)
        self.assertTrue(any("not a complete backup" in w for w in console.warned))

    def test_releases_lists_and_hints_when_no_stable_release_exists(self):
        import json

        url = "https://api.github.com/repos/handcraftedcc/Orbit12/releases?per_page=100"
        listing = [{"tag_name": "v0.2.0-pre.1", "name": "pre", "prerelease": True, "draft": False,
                    "published_at": "2026-10-10T00:00:00Z", "assets": []}]
        downloader = FakeDownloader({url: json.dumps(listing).encode()})
        world = FakeWorld()
        console = RecordingConsole()
        self.assertEqual(run_cli(["releases"], world, console, downloader), cli.EXIT_OK)
        self.assertIn("v0.2.0-pre.1", console.said[-1])
        self.assertIn("--include-prereleases", console.said[-1])

    def test_offline_install_requires_the_typed_repo_only_for_online_sources(self):
        # Offline files are not a repository, so no repository prompt appears; the flash itself
        # still needs a board. With no board the command stops before any write.
        image = make_uf2()
        uf2_path = os.path.join(self.tmp, "Orbit12-0.1.0-pre.1.uf2")
        manifest_path = os.path.join(self.tmp, "release-manifest.json")
        with open(uf2_path, "wb") as handle:
            handle.write(image)
        with open(manifest_path, "wb") as handle:
            handle.write(manifest_doc(image))
        world = FakeWorld()
        console = RecordingConsole(choices=[])
        code = run_cli(["flash", "--uf2", uf2_path, "--manifest", manifest_path, "--yes",
                        "--backup-dir", self.tmp], world, console)
        self.assertEqual(code, cli.EXIT_CANCELLED)  # no board: the backup prompt is typed FLASH
        self.assertEqual(console.typed_prompts, ["FLASH"])
        self.assertEqual(world.log, [])

    def test_non_default_repo_warning_appears_for_online_commands(self):
        import json

        console = RecordingConsole(typed=["nope"])
        url = "https://api.github.com/repos/someone/fork/releases?per_page=100"
        downloader = FakeDownloader({url: b"[]"})
        code = run_cli(["releases", "--repo", "someone/fork"], FakeWorld(), console, downloader)
        self.assertEqual(code, cli.EXIT_CANCELLED)
        self.assertTrue(any("NOT the official" in w for w in console.warned))
        self.assertEqual(downloader.requests, [])


class TerminalConsoleTests(unittest.TestCase):
    def test_typed_confirmation_ignores_yes(self):
        answers = iter(["erase"])
        console = TerminalConsole(assume_yes=True, input_fn=lambda prompt: next(answers),
                                  out=io.StringIO(), err=io.StringIO())
        self.assertFalse(console.confirm_typed("ERASE", "Really?"))

    def test_yes_answers_only_non_destructive_prompts(self):
        out = io.StringIO()
        console = TerminalConsole(assume_yes=True, input_fn=lambda prompt: "", out=out, err=io.StringIO())
        self.assertTrue(console.confirm("Flash now?", default=False))
        self.assertIn("assumed yes", out.getvalue())

    def test_default_applies_to_empty_answers(self):
        console = TerminalConsole(input_fn=lambda prompt: "", out=io.StringIO(), err=io.StringIO())
        self.assertTrue(console.confirm("Install?", default=True))
        self.assertFalse(console.confirm("Format?", default=False))

    def test_end_of_input_cancels_instead_of_guessing(self):
        def eof(prompt):
            raise EOFError()

        console = TerminalConsole(input_fn=eof, out=io.StringIO(), err=io.StringIO())
        with self.assertRaises(Cancelled):
            console.confirm_typed("ERASE", "Really?")

    def test_choose_accepts_only_listed_numbers(self):
        answers = iter(["9", "2"])
        console = TerminalConsole(input_fn=lambda prompt: next(answers), out=io.StringIO(), err=io.StringIO())
        self.assertIsNone(console.choose("Pick", ["a", "b"]))
        self.assertEqual(console.choose("Pick", ["a", "b"]), 1)


class BuildScriptTests(unittest.TestCase):
    def test_build_command_names_the_output_and_bundles_pyserial(self):
        build_exe = os.path.join(INSTALLER, "build_exe.py")
        result = subprocess.run([sys.executable, "-I", build_exe, "--print-command"],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        command = result.stdout
        self.assertIn("--onefile", command)
        self.assertIn("orbit12-installer-", command)
        self.assertIn("serial.tools.list_ports", command)
        self.assertIn("entry.py", command)


if __name__ == "__main__":
    unittest.main()
