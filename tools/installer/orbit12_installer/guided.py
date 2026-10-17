"""Headless controller for the one-window program. No tkinter here: the window is a View.

The controller runs on a worker thread and reports through a View (see class View). Every View
call can come from the worker; MarshalledView forwards them to the UI thread through a Bridge, and
blocks until the UI thread has answered. Questions therefore never run on the UI thread's loop
and never get answered by default. Tests drive the same controller with a scripted View.
"""

from __future__ import annotations

import errno
import json
import os
import queue
import sys
import threading
import traceback
from dataclasses import dataclass
from typing import Callable, List, Optional, Sequence

from . import cdc, device, flow
from .cdc import DeviceRefused, ProtocolError
from .device import Cancelled, FlashError, Hardware
from .github import Release, ReleaseError, select_release
from .interfaces import Console, PortInfo
from .package import PackageError

PROGRAM = "Orbit12 installer"
BOARD_MISSING_TEXT = ("No MacroPad was found. Connect it with its USB cable (use a data cable, not a "
                      "charge-only one). If you picked a port in the list, choose Automatic or the right "
                      "port. Then press Retry.")
SETTINGS_NAME = ".orbit12-installer.json"
BACKUP_FOLDER_NAME = "orbit12-backups"


# ---- data handed to the view ----------------------------------------------------------------

@dataclass(frozen=True)
class BoardView:
    """kind: orbit12, bootloader, circuitpython, none (not found) or error (text in `firmware`)."""
    kind: str
    port: str = ""
    firmware: str = ""
    storage: str = ""

    @property
    def present(self) -> bool:
        return self.kind not in ("none", "error")


@dataclass(frozen=True)
class ReleaseChoices:
    stable: Optional[Release]    # newest stable release, None when none is published
    newest: Optional[Release]    # newest release of any kind (prereleases included)
    error: str = ""              # plain text when the release list could not be read


@dataclass(frozen=True)
class SceneRow:
    slot: str    # 'Scene01'..'Scene16'
    label: str   # name from scene_names, or ''
    saved: bool


@dataclass(frozen=True)
class Outcome:
    status: str          # done, cancelled or failed
    title: str
    text: str
    next_step: str = ""
    details: str = ""


def pick_release(choices: ReleaseChoices, include_prereleases: bool) -> Optional[Release]:
    return choices.newest if include_prereleases else choices.stable


# ---- the View protocol (implemented by gui.TkView and by tests) -------------------------------

class View:
    """Every method is called from the controller. Question methods return the user's answer."""

    def busy(self, text: str) -> None: raise NotImplementedError  # pragma: no cover
    def idle(self) -> None: raise NotImplementedError  # pragma: no cover
    def step(self, text: str) -> None: raise NotImplementedError  # pragma: no cover
    def say(self, text: str) -> None: raise NotImplementedError  # pragma: no cover
    def warn(self, text: str) -> None: raise NotImplementedError  # pragma: no cover
    def ports(self, ports: Sequence[PortInfo]) -> None: raise NotImplementedError  # pragma: no cover
    def board(self, board: BoardView) -> None: raise NotImplementedError  # pragma: no cover
    def releases(self, choices: ReleaseChoices) -> None: raise NotImplementedError  # pragma: no cover
    def scenes(self, rows: Sequence[SceneRow]) -> None: raise NotImplementedError  # pragma: no cover
    def outcome(self, outcome: Outcome) -> None: raise NotImplementedError  # pragma: no cover
    def confirm(self, message: str, default: bool) -> bool: raise NotImplementedError  # pragma: no cover
    def confirm_typed(self, word: str, message: str) -> bool: raise NotImplementedError  # pragma: no cover
    def physical_action(self, message: str) -> bool: raise NotImplementedError  # pragma: no cover
    def board_missing(self, message: str) -> bool: raise NotImplementedError  # pragma: no cover
    def choose(self, prompt: str, options: List[str]) -> Optional[int]: raise NotImplementedError  # pragma: no cover
    def ask(self, prompt: str) -> str: raise NotImplementedError  # pragma: no cover
    def choose_folder(self, title: str) -> Optional[str]: raise NotImplementedError  # pragma: no cover
    def ask_scene_name(self, current: str, taken: Sequence[str]) -> Optional[str]: raise NotImplementedError  # pragma: no cover


class Bridge:
    """Runs View calls on the thread that created the bridge (the UI thread).

    call() from another thread queues the call and waits for its result. pump() runs queued calls
    and must be called by the UI loop (for example from Tk's after()).
    """

    def __init__(self) -> None:
        self._owner = threading.current_thread()
        self._calls: "queue.Queue" = queue.Queue()

    def call(self, fn: Callable, *args, **kwargs):
        if threading.current_thread() is self._owner:
            return fn(*args, **kwargs)
        reply: "queue.Queue" = queue.Queue(maxsize=1)
        self._calls.put((fn, args, kwargs, reply))
        ok, value = reply.get()
        if ok:
            return value
        raise value

    def pump(self, limit: int = 200) -> int:
        done = 0
        while done < limit:
            try:
                fn, args, kwargs, reply = self._calls.get_nowait()
            except queue.Empty:
                break
            try:
                reply.put((True, fn(*args, **kwargs)))
            except BaseException as error:  # handed back to the waiting worker
                reply.put((False, error))
            done += 1
        return done


class MarshalledView:
    """Forwards every View method to the UI thread through a Bridge."""

    def __init__(self, view: View, bridge: Bridge) -> None:
        self._view = view
        self._bridge = bridge

    def __getattr__(self, name: str):
        target = getattr(self._view, name)

        def call(*args, **kwargs):
            return self._bridge.call(target, *args, **kwargs)
        return call


class GuiConsole(Console):
    """The flow's Console on top of a View. Keeps the text lines for the copyable details."""

    def __init__(self, view) -> None:
        self._view = view
        self.lines: List[str] = []

    def say(self, message: str) -> None:
        self.lines.append(message)
        self._view.say(message)

    def warn(self, message: str) -> None:
        self.lines.append(f"WARNING: {message}")
        self._view.warn(message)

    def step(self, text: str) -> None:
        self._view.step(text)

    def pause_for_action(self, message: str) -> None:
        if not self._view.physical_action(message):
            raise Cancelled("stopped at the board step; nothing more was done")

    def confirm(self, message: str, default: bool = False) -> bool:
        return bool(self._view.confirm(message, default))

    def confirm_typed(self, word: str, message: str) -> bool:
        return bool(self._view.confirm_typed(word, message))

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:
        return self._view.choose(prompt, options)

    def ask(self, prompt: str) -> str:
        return self._view.ask(prompt)


# ---- detection and releases ------------------------------------------------------------------

def detect_board(hw: Hardware, opts: flow.Options) -> BoardView:
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is not None:
        report = orbit.info
        if report is not None:
            firmware = f"Orbit12 {report.fw} (build {report.sha})"
            storage = (f"{report.scenes} saved scene(s)" if report.storage_mounted
                       else "not set up yet (the install can set it up)")
        else:
            version = orbit.status.version
            firmware = (f"Orbit12 {version.semver} (build {version.git_sha})" if version is not None
                        else "Orbit12 (version not reported by older firmware)")
            storage = "ready" if orbit.storage_mounted else "not set up yet"
        return BoardView("orbit12", orbit.port.device, firmware, storage)
    bootloader = device.single_bootsel_volume(hw)
    if bootloader is not None:
        return BoardView("bootloader", bootloader.path)
    lone = device.lone_cdc_port(hw)
    if lone or device.circuitpy_volumes(hw):
        return BoardView("circuitpython", lone or "")
    return BoardView("none")


def fetch_release_choices(hw: Hardware, downloader, opts: flow.Options) -> ReleaseChoices:
    releases = flow.list_releases(hw, downloader, opts)
    stable = newest = None
    try:
        stable = select_release(releases, include_prereleases=False)
    except ReleaseError:
        pass
    try:
        newest = select_release(releases, include_prereleases=True)
    except ReleaseError:
        pass
    return ReleaseChoices(stable, newest)


# ---- friendly errors and the copyable details -----------------------------------------------

EXPECTED_ERRORS = (Cancelled, FlashError, ReleaseError, PackageError, DeviceRefused, ProtocolError,
                   TimeoutError, OSError, ValueError)


def _plain(text: str) -> str:
    if "several Orbit12 boards" in text:
        return ("Two or more Orbit12 boards are connected. Unplug all but one, choose its port or "
                "Automatic, then press Retry.")
    return text.replace("`orbit12-installer format`", "the Install button (it asks before formatting)")


def build_details(error: Optional[BaseException], log: Sequence[str], include_traceback: bool) -> str:
    parts = [PROGRAM, ""]
    if error is not None:
        parts.append(f"Error: {type(error).__name__}: {error}")
        if include_traceback:
            parts.append("".join(traceback.format_exception(type(error), error, error.__traceback__)).rstrip())
    if log:
        parts.append("")
        parts.append("Log:")
        parts.extend(log)
    return "\n".join(parts)


def describe_error(error: BaseException, log: Sequence[str]) -> Outcome:
    """Turns any exception into a screen: what happened, and what to do next. No tracebacks."""
    if isinstance(error, Cancelled):
        return Outcome("cancelled", "Stopped", _plain(str(error)) or "Stopped at your request.",
                       "Nothing more was done. Press the button again when you are ready.",
                       build_details(error, log, False))
    if isinstance(error, (ReleaseError, PackageError)):
        return Outcome("failed", "The release could not be used", _plain(str(error)),
                       "Nothing was written to the board. Check your internet connection and try again. "
                       "If it keeps happening, press Copy details and send them to support.",
                       build_details(error, log, False))
    if isinstance(error, OSError) and error.errno in (errno.EACCES, errno.EPERM) and sys.platform.startswith("linux"):
        return Outcome("failed", "Linux does not let this program use the USB serial port", str(error),
                       "In a terminal run: sudo usermod -aG dialout $USER . Then log out and back in, "
                       "and press Retry.", build_details(error, log, False))
    if isinstance(error, EXPECTED_ERRORS):
        return Outcome("failed", "The install stopped", _plain(str(error)),
                       "Read the message. Keep any backup folder it names. Unplug the MacroPad, plug it in "
                       "again and press Retry. Press Copy details if you need help.",
                       build_details(error, log, False))
    return Outcome("failed", "Something unexpected went wrong",
                   f"{type(error).__name__}: {error}" if str(error) else type(error).__name__,
                   "Press Copy details and send the text to support. Nothing else was done by this step.",
                   build_details(error, log, True))


# ---- settings (remembered port) -------------------------------------------------------------

def default_settings_path() -> str:
    return os.path.join(os.path.expanduser("~"), SETTINGS_NAME)


def load_settings(path: Optional[str]) -> dict:
    if not path:
        return {}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def save_settings(path: Optional[str], data: dict) -> None:
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(data, handle, indent=2)
    except OSError:
        pass  # remembering the port is a convenience; the program works without it


# ---- the session --------------------------------------------------------------------------

class Session:
    """Runs one user action at a time. Each public action reports through the view and never raises."""

    def __init__(self, view, *, ports, opener, volumes, clock, downloader, opts: flow.Options,
                 settings_path: Optional[str] = None) -> None:
        self.view = view
        self.console = GuiConsole(view)
        self.hw = Hardware(ports=ports, opener=opener, volumes=volumes, clock=clock, console=self.console)
        self.downloader = downloader
        self.opts = opts
        self.settings_path = settings_path
        self._scene_rows: Optional[List[SceneRow]] = None

    # actions (call from a worker thread)

    def refresh_ports(self) -> None:
        self._guard("Looking for serial ports...", self._refresh_ports)

    def set_port(self, port: Optional[str]) -> None:
        self.opts.port = port
        settings = load_settings(self.settings_path)
        if port:
            settings["port"] = port
        else:
            settings.pop("port", None)
        save_settings(self.settings_path, settings)

    def refresh(self) -> None:
        self._guard("Looking for your MacroPad and the newest release...", self._refresh)

    def install(self, release: Release) -> None:
        self._guard("Installing Orbit12. Keep the MacroPad plugged in.", self._install, release)

    def load_scenes(self) -> None:
        self._guard("Reading the scenes on the board...", self._load_scenes)

    def backup_scenes(self, slot: Optional[str]) -> None:
        self._guard("Backing up scenes...", self._backup_scenes, slot)

    def restore_scenes(self, one: bool) -> None:
        self._guard("Restoring scenes...", self._restore_scenes, one)

    def rename_scene(self, slot: str) -> None:
        self._guard("Renaming the scene...", self._rename_scene, slot)

    # internals

    def _guard(self, label: str, fn, *args) -> None:
        self.view.busy(label)
        try:
            result = fn(*args)
            if isinstance(result, Outcome):
                self.view.outcome(result)
        except Exception as error:  # every failure becomes a screen; tracebacks go to Copy details only
            self.view.outcome(describe_error(error, self.console.lines))
        finally:
            self.view.idle()

    def _refresh_ports(self) -> None:
        self.view.ports(self.hw.ports.list_ports())

    def _refresh(self) -> None:
        try:
            board = detect_board(self.hw, self.opts)
        except FlashError as error:
            board = BoardView("error", firmware=_plain(str(error)))
        self.view.board(board)
        try:
            choices = fetch_release_choices(self.hw, self.downloader, self.opts)
        except Exception as error:
            choices = ReleaseChoices(None, None, error=describe_error(error, []).text)
        self.view.releases(choices)

    def _await_board(self) -> None:
        while not detect_board(self.hw, self.opts).present:
            if not self.view.board_missing(BOARD_MISSING_TEXT):
                raise Cancelled("No MacroPad was found, so nothing was changed.")

    def _await_orbit(self) -> device.Orbit12Device:
        while True:
            orbit = device.find_orbit12(self.hw, self.opts.port)
            if orbit is not None:
                return orbit
            if not self.view.board_missing(BOARD_MISSING_TEXT):
                raise Cancelled("No Orbit12 board was found, so nothing was changed.")

    def _install(self, release: Release) -> Outcome:
        self._await_board()
        package = flow.download_package(self.hw, self.downloader, release, self.opts)
        summary = flow.run_flash(self.hw, package, self.opts, interactive=True)
        return Outcome("done", f"Orbit12 {release.tag} is installed", summary,
                       "Check that the MacroPad starts normally and that your scenes are there. If your music "
                       "software does not see the MacroPad, restart it (it keeps the old USB connection). Keep the "
                       f"backup folder in {self.opts.backup_root or BACKUP_FOLDER_NAME} until you have "
                       "checked them.")

    def _load_scenes(self) -> None:
        orbit = self._await_orbit()
        saved, names = flow.scenes_list(self.hw, orbit)
        rows = [SceneRow(slot, names.get(slot, ""), saved[slot]) for slot in cdc.SCENE_NAMES]
        self._scene_rows = rows
        self.view.scenes(rows)

    def _backup_scenes(self, slot: Optional[str]) -> Outcome:
        if slot is not None and not self._slot_saved(slot):
            return Outcome("cancelled", "Nothing to back up", f"{slot} is empty on the board.")
        orbit = self._await_orbit()
        folder = flow.scenes_backup(self.hw, orbit, self.opts, None if slot is None else [slot])
        what = "All scenes and settings were" if slot is None else f"{slot} was"
        return Outcome("done", "Backup saved", f"{what} saved to {folder}.",
                       "Keep this folder. You can restore from it on the Scenes tab.")

    def _rename_scene(self, slot: str) -> Outcome:
        rows = self._scene_rows
        if rows is None:
            raise FlashError("show the scenes on the board first, then choose one to rename")
        row = next((r for r in rows if r.slot == slot), None)
        if row is None or not row.saved or not row.label:
            return Outcome("cancelled", "Nothing to rename", f"{slot} has no name on the board.")
        orbit = self._await_orbit()
        flow.require_rename(orbit)  # refused before any name is asked for, on firmware without RENAME
        taken = [other.label for other in rows if other.slot != slot and other.label]
        typed = self.view.ask_scene_name(row.label, taken)
        if typed is None:
            raise Cancelled("no new name was given; nothing was changed")
        name = cdc.normalize_scene_name(typed)
        if name == row.label:
            return Outcome("cancelled", "Name not changed", f"{slot} is already called {name}.")
        problem = cdc.scene_name_problem(name, taken)
        if problem is not None:
            raise FlashError(f"{problem} Nothing was changed.")
        text = flow.scenes_rename(self.hw, orbit, slot, name)
        self._scene_rows = [SceneRow(r.slot, name if r.slot == slot else r.label, r.saved) for r in rows]
        self.view.scenes(self._scene_rows)
        return Outcome("done", "Scene renamed", text,
                       "The new name is saved on the MacroPad and shows at once; no restart is needed.")

    def _slot_saved(self, slot: str) -> bool:
        """True when the last listing showed the slot saved, or when no listing is known yet."""
        if self._scene_rows is None:
            return True
        return any(row.slot == slot and row.saved for row in self._scene_rows)

    def _restore_scenes(self, one: bool) -> Outcome:
        folder = self.view.choose_folder("Choose the backup folder (the one that holds backup.json)")
        if not folder:
            raise Cancelled("no backup folder was chosen; nothing was changed")
        files = device.load_backup(folder)  # verified before the board is touched
        names: Optional[List[str]] = None
        if one:
            offered = [name for name, _ in files if name in cdc.SCENE_NAMES]
            if not offered:
                raise FlashError("this backup holds no scenes; choose a different folder")
            index = self.view.choose("Which scene do you want to restore?", offered)
            if index is None:
                raise Cancelled("no scene was chosen; nothing was changed")
            names = [offered[index]]
        orbit = self._await_orbit()
        restored = flow.scenes_restore(self.hw, orbit, folder, names)
        return Outcome("done", "Scenes restored", "Restored: " + (", ".join(restored) or "nothing") + ".",
                       "Check the scenes on the MacroPad.")
