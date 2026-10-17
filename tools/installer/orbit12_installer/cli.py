"""Command line and the no-argument wizard. Exit codes: 0 done, 1 failed, 2 usage, 3 cancelled."""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional

from . import device, flow
from .cdc import DeviceRefused, ProtocolError
from .console import TerminalConsole
from .device import Cancelled, FlashError, Hardware
from .github import DEFAULT_REPO, HttpDownloader, NoStableRelease, ReleaseError, select_release
from .hardware import PySerialEnumerator, PySerialOpener, RealClock, SystemVolumeFinder
from .interfaces import Console, Downloader
from .package import PackageError

EXIT_OK, EXIT_FAILED, EXIT_USAGE, EXIT_CANCELLED = 0, 1, 2, 3
INSTALLER_VERSION = "1.0"


def _common_parser(suppress: bool) -> argparse.ArgumentParser:
    """Shared options. Subcommands suppress defaults so they never overwrite options given before them."""
    d = argparse.SUPPRESS if suppress else None
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--repo", default=DEFAULT_REPO if d is None else d,
                        help=f"GitHub repository owner/name (default {DEFAULT_REPO})")
    common.add_argument("--include-prereleases", action="store_true", default=d,
                        help="pick the newest release even if it is a prerelease")
    common.add_argument("--port", default=d, help="serial port to use, e.g. COM5 or /dev/ttyACM0")
    common.add_argument("--backup-dir", dest="backup_root", default=d,
                        help="folder for backups (default: next to the program)")
    common.add_argument("--yes", action="store_true", default=d,
                        help="answer yes to non-destructive questions (never to typed confirmations)")
    common.add_argument("--pause", action="store_true", default=d, help="wait for Enter before exiting")
    return common


def _source_parser(suppress: bool) -> argparse.ArgumentParser:
    d = argparse.SUPPRESS if suppress else None
    source = argparse.ArgumentParser(add_help=False)
    source.add_argument("--tag", default=d, help="install this exact release tag")
    source.add_argument("--uf2", default=d, help="offline: a local Orbit12-<version>.uf2")
    source.add_argument("--manifest", default=d, help="offline: the matching release-manifest.json")
    source.add_argument("--sums", default=d, help="offline (optional): the matching SHA256SUMS")
    return source


def build_parser() -> argparse.ArgumentParser:
    common = _common_parser(suppress=True)
    source = _source_parser(suppress=True)
    parser = argparse.ArgumentParser(
        prog="orbit12-installer",
        description="Install or update Orbit12 firmware on an Adafruit MacroPad RP2040. "
                    "Run with no arguments for a guided wizard.",
        parents=[_common_parser(suppress=False), _source_parser(suppress=False)])
    parser.add_argument("--version", action="version", version=f"orbit12-installer {INSTALLER_VERSION}")
    parser.add_argument("--cli", action="store_true",
                        help="use the text wizard instead of the window (with no command)")
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("install", parents=[common, source],
                   help="download the latest release, verify it and flash it (default wizard)")
    sub.add_parser("flash", parents=[common, source],
                   help="flash a verified release (non-interactive selection; --tag or offline files)")
    sub.add_parser("releases", parents=[common], help="list published releases")
    sub.add_parser("info", parents=[common], help="show the firmware and storage state of the board")
    sub.add_parser("backup", parents=[common], help="save scenes and settings to a backup folder")
    restore = sub.add_parser("restore", parents=[common], help="write a backup folder back to the board")
    restore.add_argument("--from", dest="source_folder", required=True, help="backup folder")
    sub.add_parser("format", parents=[common],
                   help="erase and format the scene partition (asks you to type ERASE)")
    return parser


def _hardware(console: Console) -> Hardware:
    return Hardware(ports=PySerialEnumerator(), opener=PySerialOpener(), volumes=SystemVolumeFinder(),
                    clock=RealClock(), console=console)


def _options(args) -> flow.Options:
    return flow.Options(
        repo=args.repo,
        include_prereleases=args.include_prereleases,
        tag=getattr(args, "tag", None),
        uf2=getattr(args, "uf2", None),
        manifest=getattr(args, "manifest", None),
        sums=getattr(args, "sums", None),
        port=args.port,
        backup_root=args.backup_root,
    )


def _install(hw: Hardware, downloader: Downloader, opts: flow.Options, interactive: bool) -> str:
    if opts.uf2 or opts.manifest:
        package = flow.load_local_package(opts)
    else:
        release = flow.pick_release(hw, downloader, opts, interactive)
        package = flow.download_package(hw, downloader, release, opts)
    return flow.run_flash(hw, package, opts, interactive=interactive)


def _releases(hw: Hardware, downloader: Downloader, opts: flow.Options) -> str:
    releases = flow.list_releases(hw, downloader, opts)
    if not releases:
        return "No published releases."
    lines = []
    for release in releases:
        kind = "prerelease" if release.prerelease else "stable"
        lines.append(f"{release.tag:<18} {kind:<10} {release.published_at[:10]}  {release.name}")
    try:
        default = select_release(releases, opts.include_prereleases)
        lines.append(f"Default install: {default.tag}")
    except NoStableRelease as error:
        lines.append(str(error))
    return "\n".join(lines)


def _wizard(hw: Hardware, downloader: Downloader, console: Console, opts: flow.Options) -> int:
    console.say(f"Orbit12 installer {INSTALLER_VERSION}")
    console.say("Connect the MacroPad with its USB cable. Nothing is written until you confirm.")
    choice = console.choose("What do you want to do?", [
        "Install the latest Orbit12 firmware (backs up scenes first)",
        "Back up scenes and settings",
        "Restore a backup folder",
        "Show board information",
        "Format the scene partition (erases everything; asks for ERASE)",
        "Quit",
    ])
    if choice is None or choice == 5:  # None: an invalid answer ends the wizard without changes
        return EXIT_OK
    if choice == 0:
        console.say(_install(hw, downloader, opts, interactive=True))
    elif choice == 1:
        console.say(flow.backup_only(hw, opts))
    elif choice == 2:
        folder = console.ask("Backup folder to restore (the folder holding backup.json)")
        console.say(flow.restore_only(hw, opts, folder))
    elif choice == 3:
        console.say(flow.info(hw, opts))
    elif choice == 4:
        console.say(flow.format_only(hw, opts))
    return EXIT_OK


def run(argv: Optional[List[str]] = None, *, console: Optional[Console] = None,
        hardware: Optional[Hardware] = None, downloader: Optional[Downloader] = None) -> int:
    """Testable entry point. Real hardware, console and downloader are used when not supplied."""
    parser = build_parser()
    raw = list(sys.argv[1:] if argv is None else argv)
    args = parser.parse_args(raw)
    own_console = console is None
    interactive_console = console or TerminalConsole(assume_yes=args.yes)
    hw = hardware or _hardware(interactive_console)
    dl = downloader or HttpDownloader()
    opts = _options(args)
    wizard = args.command is None
    tty = getattr(sys.stdin, "isatty", lambda: False)()
    pause = own_console and (args.pause or (wizard and tty))
    try:
        if wizard:
            code = _wizard(hw, dl, interactive_console, opts)
        elif args.command == "install":
            interactive_console.say(_install(hw, dl, opts, interactive=True))
            code = EXIT_OK
        elif args.command == "flash":
            interactive_console.say(_install(hw, dl, opts, interactive=not args.yes))
            code = EXIT_OK
        elif args.command == "releases":
            interactive_console.say(_releases(hw, dl, opts))
            code = EXIT_OK
        elif args.command == "info":
            interactive_console.say(flow.info(hw, opts))
            code = EXIT_OK
        elif args.command == "backup":
            interactive_console.say(flow.backup_only(hw, opts))
            code = EXIT_OK
        elif args.command == "restore":
            interactive_console.say(flow.restore_only(hw, opts, args.source_folder))
            code = EXIT_OK
        elif args.command == "format":
            interactive_console.say(flow.format_only(hw, opts))
            code = EXIT_OK
        else:  # pragma: no cover - argparse already rejects unknown commands
            parser.error("unknown command")
            code = EXIT_USAGE
    except Cancelled as error:
        interactive_console.warn(str(error))
        code = EXIT_CANCELLED
    except (FlashError, ReleaseError, PackageError, DeviceRefused, ProtocolError,
            TimeoutError, OSError, ValueError) as error:
        interactive_console.warn(f"stopped: {error}")
        code = EXIT_FAILED
    except KeyboardInterrupt:
        interactive_console.warn("stopped by the user; nothing further was done")
        code = EXIT_CANCELLED
    if pause:
        try:
            input("Press Enter to exit ")
        except (EOFError, KeyboardInterrupt, RuntimeError):  # RuntimeError: no stdin in a windowed program
            pass
    return code


def main(argv: Optional[List[str]] = None) -> int:
    """Program entry. No arguments opens the window (when one can open); anything else is the CLI."""
    raw = list(sys.argv[1:] if argv is None else argv)
    if not raw:
        from . import gui
        code = gui.run_app()
        if code is not None:
            return code  # the window ran; None means no window could open, so use the text wizard
    return run(raw)
