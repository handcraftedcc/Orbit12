"""Tkinter window (stdlib only). All logic is in guided.py; this module only draws and asks.

tkinter is imported inside functions, so importing this module never needs a display. Worker
threads call the view through guided.Bridge; this module pumps the bridge with after() on the UI
thread, so every widget is touched only from the UI thread.

The look (dark, flat, monospace, terminal-style) lives in theme.py. Native message boxes cannot be
themed, so every question is asked through a themed modal Toplevel. The folder picker stays the
system dialog; Tk offers no way to restyle it.
"""

from __future__ import annotations

import os
import sys
import threading
from typing import List, Optional, Sequence

from . import flow, guided
from .guided import BoardView, Bridge, MarshalledView, Outcome, ReleaseChoices, SceneRow, Session, View
from .hardware import PySerialEnumerator, PySerialOpener, RealClock, SystemVolumeFinder
from .github import HttpDownloader
from .interfaces import PortInfo
from .theme import ACCENT, BG, ERROR, INFO, MUTED, RULE, SELECT, TEXT, apply_theme, enable_dpi_awareness
from .theme import flat_button, rule, set_flat_colour

AUTOMATIC = "Automatic (find my Orbit12)"
PUMP_MS = 50
PROGRESS_MS = 120
TITLE = "Orbit12 Installer"
TABS = ("INSTALL", "SCENES", "MODULES")
PRERELEASE_TEXT = "Include test (pre-release) versions"


def available() -> bool:
    """True when a window can plausibly open. Linux needs a display; Windows and macOS always have one."""
    if sys.platform.startswith("linux") and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        return False
    try:
        import tkinter  # noqa: F401
    except ImportError:
        return False
    return True


def run_app() -> Optional[int]:
    """Opens the window and returns 0 when it closes. Returns None when no window can be opened."""
    if not available():
        return None
    enable_dpi_awareness()  # before Tk() so text is not bitmap-scaled on high-DPI screens
    import tkinter as tk
    try:
        root = tk.Tk()
    except tk.TclError:
        return None
    try:
        InstallerWindow(root).start()
        root.mainloop()
    finally:
        try:
            root.destroy()
        except tk.TclError:
            pass
    return 0


class InstallerWindow:
    def __init__(self, root) -> None:
        import tkinter as tk
        from tkinter import ttk
        self.tk = tk
        self.ttk = ttk
        self.root = root
        self.fonts = {}
        self._busy = False
        self._choices = ReleaseChoices(None, None)
        self._ports: List[PortInfo] = []
        self._port_values: List[Optional[str]] = []
        self._details = ""
        self._result: Optional[Outcome] = None
        self._scene_rows: List[SceneRow] = []
        self._alive = True
        self._progress_job = None
        self._progress_pos = 0
        self._tab_buttons: list = []

        settings_path = guided.default_settings_path()
        settings = guided.load_settings(settings_path)
        remembered = settings.get("port") if isinstance(settings.get("port"), str) else None
        self._view = TkView(self)
        self.bridge = Bridge()
        self.session = Session(
            MarshalledView(self._view, self.bridge),
            ports=PySerialEnumerator(), opener=PySerialOpener(), volumes=SystemVolumeFinder(),
            clock=RealClock(), downloader=HttpDownloader(), opts=flow.Options(port=remembered),
            settings_path=settings_path)
        self.remembered = remembered
        self._build()

    # ---- layout ----

    def _build(self) -> None:
        tk, ttk, root = self.tk, self.ttk, self.root
        self.fonts = apply_theme(root)
        f = self.fonts
        root.title(TITLE)
        root.geometry("660x700")
        root.minsize(560, 600)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        root.report_callback_exception = self._on_tk_error

        # Packing order matters: footer widgets are packed (side="bottom") before the expanding notebook.
        head = tk.Frame(root, bg=BG)
        head.pack(side="top", fill="x", padx=16, pady=(14, 6))
        self._label(head, "Orbit12 Installer", bold=True, title=True).pack(anchor="w")
        self._label(head, "Install or update Orbit12 on your MacroPad. Your scenes are backed up first.",
                    colour=MUTED, wrap=600).pack(anchor="w", pady=(2, 0))
        port_row = tk.Frame(head, bg=BG)
        port_row.pack(anchor="w", fill="x", pady=(10, 0))
        self._label(port_row, "MacroPad port:", colour=MUTED).pack(side="left")
        self.port_box = ttk.Combobox(port_row, state="readonly", width=40, font=f["body"])
        self.port_box.pack(side="left", padx=8)
        self.port_box.bind("<<ComboboxSelected>>", self._on_port_chosen)
        self.ports_btn = flat_button(port_row, f, "Refresh ports", self._refresh_ports)
        self.ports_btn.pack(side="left", padx=(6, 0))

        rule(root, side="top", pad=(12, 0))
        tabs = tk.Frame(root, bg=BG)
        tabs.pack(side="top", fill="x", padx=16, pady=(8, 0))
        for index, name in enumerate(TABS):
            button = flat_button(tabs, f, name, lambda i=index: self._select_tab(i))
            button.pack(side="left", padx=(0, 16))
            self._tab_buttons.append(button)

        bottom = tk.Frame(root, bg=BG)
        bottom.pack(side="bottom", fill="x", padx=16, pady=(0, 12))
        self.progress_var = tk.StringVar(value="")
        self.progress_label = tk.Label(bottom, textvariable=self.progress_var, font=f["mono"], fg=INFO,
                                       bg=BG, anchor="w", justify="left")
        self.progress_label.pack(side="bottom", fill="x", pady=(4, 0))
        status_row = tk.Frame(bottom, bg=BG)
        status_row.pack(side="bottom", fill="x", pady=(6, 0))
        self.status_var = tk.StringVar(value="Starting...")
        self._label(status_row, "", colour=MUTED, textvariable=self.status_var).pack(side="left")
        self.quit_btn = flat_button(status_row, f, "Quit", self._on_close)
        self.quit_btn.pack(side="right")
        self.result_frame = tk.Frame(bottom, bg=BG)
        self.result_frame.pack(side="top", fill="x", pady=(0, 6))
        self.result_title = self._label(self.result_frame, "", bold=True)
        self.result_title.pack(anchor="w")
        self.result_text = self._label(self.result_frame, "", colour=MUTED, wrap=600)
        self.result_text.pack(anchor="w")
        self.result_next = self._label(self.result_frame, "", wrap=600)
        self.result_next.pack(anchor="w", pady=(4, 0))
        self.copy_btn = flat_button(self.result_frame, f, "Copy details", self._copy_details)
        self.copy_btn.pack(anchor="w", pady=(4, 0))
        rule(bottom, side="bottom", pad=(0, 0))

        self.notebook = ttk.Notebook(root, style="Hidden.TNotebook")
        self.notebook.pack(side="top", fill="both", expand=True, padx=16, pady=(8, 8))
        self._build_install_tab()
        self._build_scenes_tab()
        self._build_modules_tab()
        self._select_tab(0)

    def _label(self, parent, text: str, colour: str = TEXT, bold: bool = False, title: bool = False,
               wrap: int = 0, textvariable=None):
        tk, f = self.tk, self.fonts
        font = f["title"] if title else (f["bold"] if bold else f["body"])
        kwargs = dict(text=text, font=font, fg=colour, bg=BG, anchor="w", justify="left")
        if wrap:
            kwargs["wraplength"] = wrap
        if textvariable is not None:
            kwargs["textvariable"] = textvariable
        return tk.Label(parent, **kwargs)

    def _select_tab(self, index: int) -> None:
        self.notebook.select(index)
        for position, (name, button) in enumerate(zip(TABS, self._tab_buttons)):
            active = position == index
            button.configure(text=f"[ {name} ]" if active else name)
            set_flat_colour(button, ACCENT if active else MUTED)

    def _build_install_tab(self) -> None:
        tk, f = self.tk, self.fonts
        tab = tk.Frame(self.notebook, bg=BG)
        self.notebook.add(tab, text="Install or update")
        self.board_label = self._label(tab, "Looking for your MacroPad...", wrap=600)
        self.board_label.pack(anchor="w")
        self.installed_label = self._label(tab, "Installed firmware: -", colour=MUTED, wrap=600)
        self.installed_label.pack(anchor="w", pady=(6, 0))
        self.storage_label = self._label(tab, "Scene storage: -", colour=MUTED, wrap=600)
        self.storage_label.pack(anchor="w")
        self.latest_label = self._label(tab, "Newest version: -", colour=MUTED, wrap=600)
        self.latest_label.pack(anchor="w", pady=(6, 0))
        self.include_var = tk.BooleanVar(value=False)
        self.include_btn = flat_button(tab, f, "", self._toggle_prerelease)
        self.include_btn.pack(anchor="w", pady=(6, 0))
        self._update_include_text()
        buttons = tk.Frame(tab, bg=BG)
        buttons.pack(anchor="w", pady=(8, 0), fill="x")
        self.install_btn = flat_button(buttons, f, "> Install / update", self._install_clicked, fg=ACCENT,
                                       bold=True)
        self.install_btn.pack(side="left")
        self.check_btn = flat_button(buttons, f, "Check again", self._refresh)
        self.check_btn.pack(side="left", padx=(18, 0))
        self.step_var = tk.StringVar(value="")
        self._label(tab, "", colour=INFO, wrap=600, textvariable=self.step_var).pack(anchor="w", pady=(10, 0))
        log_frame = tk.Frame(tab, bg=BG)
        log_frame.pack(fill="both", expand=True, pady=(8, 0))
        self.log = tk.Text(log_frame, height=8, wrap="word", state="disabled", font=f["mono"], bg=BG, fg=TEXT,
                           relief="flat", bd=0, highlightthickness=1, highlightbackground=RULE,
                           highlightcolor=RULE, insertbackground=TEXT, selectbackground=INFO,
                           selectforeground=TEXT, padx=8, pady=6, cursor="arrow")
        self.log.tag_configure("prompt", foreground=INFO)
        self.log.tag_configure("ok", foreground=ACCENT)
        self.log.tag_configure("warn", foreground=ERROR)
        self.log.pack(side="left", fill="both", expand=True)
        scroll = self.ttk.Scrollbar(log_frame, command=self.log.yview)
        scroll.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=scroll.set)

    def _build_scenes_tab(self) -> None:
        tk, f = self.tk, self.fonts
        tab = tk.Frame(self.notebook, bg=BG)
        self.notebook.add(tab, text="Scenes")
        self._label(tab, "Scenes on the MacroPad. Backups are saved next to the program. "
                         "Restoring replaces what is on the board, and asks you first.",
                    colour=MUTED, wrap=600).pack(anchor="w")
        list_frame = tk.Frame(tab, bg=BG)
        list_frame.pack(fill="both", expand=True, pady=8)
        self.scene_list = tk.Listbox(list_frame, height=12, font=f["mono"], bg=BG, fg=TEXT, relief="flat",
                                     bd=0, highlightthickness=1, highlightbackground=RULE,
                                     highlightcolor=INFO, selectbackground=SELECT, selectforeground=ACCENT,
                                     activestyle="none", exportselection=False)
        self.scene_list.pack(side="left", fill="both", expand=True)
        scroll = self.ttk.Scrollbar(list_frame, command=self.scene_list.yview)
        scroll.pack(side="right", fill="y")
        self.scene_list.configure(yscrollcommand=scroll.set)
        grid = tk.Frame(tab, bg=BG)
        grid.pack(anchor="w", fill="x")
        self.scene_buttons = [
            flat_button(grid, f, "Show scenes on the board", self._scenes_show),
            flat_button(grid, f, "Back up all scenes", lambda: self._start(self.session.backup_scenes, None)),
            flat_button(grid, f, "Back up one scene...", self._scene_backup_one),
            flat_button(grid, f, "Restore all from a backup...",
                        lambda: self._start(self.session.restore_scenes, False)),
            flat_button(grid, f, "Restore one scene from a backup...",
                        lambda: self._start(self.session.restore_scenes, True)),
        ]
        for index, button in enumerate(self.scene_buttons):
            button.grid(row=index // 2, column=index % 2, sticky="w", padx=(0, 24), pady=2)

    def _build_modules_tab(self) -> None:
        tab = self.tk.Frame(self.notebook, bg=BG)
        self.notebook.add(tab, text="Modules")
        self._label(tab, "Modules: coming soon.\n\nThis tab will let you switch modules on and off "
                         "once the firmware supports it. Nothing here changes the board.",
                    colour=MUTED, wrap=560).pack(anchor="w")

    # ---- actions (UI thread) ----

    def start(self) -> None:
        self._start(self._initial)
        self.root.after(PUMP_MS, self._pump)

    def _initial(self) -> None:
        self.session.refresh_ports()
        self.session.refresh()

    def _pump(self) -> None:
        if not self._alive:
            return
        self.bridge.pump()
        self.root.after(PUMP_MS, self._pump)

    def _start(self, fn, *args) -> None:
        if self._busy:
            return
        self._busy = True
        self._apply_controls()
        threading.Thread(target=fn, args=args, daemon=True).start()

    def _refresh(self) -> None:
        self._start(self.session.refresh)

    def _refresh_ports(self) -> None:
        self._start(self.session.refresh_ports)

    def _install_clicked(self) -> None:
        release = guided.pick_release(self._choices, self.include_var.get())
        if release is not None:
            self._start(self.session.install, release)

    def _scenes_show(self) -> None:
        self._start(self.session.load_scenes)

    def _scene_backup_one(self) -> None:
        slot = self._choose_slot()
        if slot is not None:
            self._start(self.session.backup_scenes, slot)

    def _choose_slot(self) -> Optional[str]:
        saved = [row for row in self._scene_rows if row.saved]
        if not saved:
            self._view.warn("Show the scenes on the board first, then choose one to back up.")
            return None
        index = self._view.choose("Which scene do you want to back up?", [_scene_text(r) for r in saved])
        return None if index is None else saved[index].slot

    def _on_port_chosen(self, _event=None) -> None:
        index = self.port_box.current()
        if index < 0 or self._busy:
            return
        port = self._port_values[index]
        self.session.set_port(port)
        self._start(self.session.refresh)

    def _toggle_prerelease(self) -> None:
        self.include_var.set(not self.include_var.get())
        self._update_include_text()
        self._update_install_button()

    def _update_include_text(self) -> None:
        on = self.include_var.get()
        self.include_btn.configure(text=("[x] " if on else "[ ] ") + PRERELEASE_TEXT)
        set_flat_colour(self.include_btn, ACCENT if on else MUTED)

    def _update_install_button(self) -> None:
        self._apply_controls()

    def _apply_controls(self) -> None:
        state = "disabled" if self._busy else "normal"
        for widget in (self.check_btn, self.ports_btn, *self.scene_buttons):
            widget.configure(state=state)
        self.port_box.configure(state="disabled" if self._busy else "readonly")
        release = guided.pick_release(self._choices, self.include_var.get())
        if release is not None and not self._busy:
            self.install_btn.configure(state="normal", text=f"> Install / update to {release.tag}")
        else:
            self.install_btn.configure(state="disabled", text="> Install / update")
        if self._busy:
            self._start_progress()
        else:
            self.progress_var.set("")

    def _start_progress(self) -> None:
        if self._progress_job is None:
            self._tick_progress()

    def _tick_progress(self) -> None:
        """Animates a bouncing ASCII bar while busy. The work has no known total, so no percentage is shown."""
        self._progress_job = None
        if not self._alive:
            return
        if not self._busy:
            self.progress_var.set("")
            return
        width, block = 28, 6
        span = width - block
        pos = self._progress_pos % (2 * span)
        head = pos if pos <= span else 2 * span - pos
        self.progress_var.set("[" + "-" * head + "#" * block + "-" * (span - head) + "]  working")
        self._progress_pos += 1
        self._progress_job = self.root.after(PROGRESS_MS, self._tick_progress)

    def _copy_details(self) -> None:
        self.root.clipboard_clear()
        self.root.clipboard_append(self._details)
        self.status_var.set("Details copied. Paste them into your message to support.")

    def _on_close(self) -> None:
        if self._busy:
            self._modal(TITLE, "The installer is still working. Closing now could leave the board half-written. "
                               "Wait until it says it is finished, then close.", [("OK", None)], primary_index=0)
            return
        self._alive = False
        self.root.destroy()

    def _on_tk_error(self, exc_type, value, _tb) -> None:
        self._modal(TITLE, "Something unexpected happened in the window. Close the installer and open it again. "
                           "If it keeps happening, tell support.", [("OK", None)], primary_index=0)

    # ---- what the view shows (called on the UI thread via the view) ----

    def set_busy(self, text: str) -> None:
        self._busy = True
        self.status_var.set(text)
        self._apply_controls()

    def set_idle(self) -> None:
        self._busy = False
        self.status_var.set("Ready.")
        self.step_var.set("")
        self._apply_controls()

    # ---- themed modal dialogs (replace the native message boxes and simple dialogs) ----

    def _toplevel(self, title: str):
        tk = self.tk
        top = tk.Toplevel(self.root, background=BG)
        top.title(title)
        top.transient(self.root)
        top.resizable(False, False)
        body = tk.Frame(top, bg=BG, padx=20, pady=16)
        body.grid(row=0, column=0)
        return top, body

    def _entry(self, parent, variable, width: int):
        return self.tk.Entry(parent, textvariable=variable, width=width, font=self.fonts["mono"], bg=BG, fg=TEXT,
                             insertbackground=TEXT, relief="flat", bd=0, highlightthickness=1,
                             highlightbackground=RULE, highlightcolor=INFO, selectbackground=INFO,
                             selectforeground=TEXT)

    def _wait(self, top) -> None:
        top.grab_set()
        top.wait_window()

    def _modal(self, title: str, message: str, buttons, primary_index: Optional[int] = None):
        """Shows message with one flat button per (text, value); returns the value picked, or None when closed."""
        top, body = self._toplevel(title)
        self._label(body, message, wrap=460).grid(row=0, column=0, sticky="w")
        answer = {"value": None}

        def pick(value):
            answer["value"] = value
            top.destroy()
        panel = self.tk.Frame(body, bg=BG)
        panel.grid(row=1, column=0, sticky="w", pady=(14, 0))
        horizontal = len(buttons) <= 3
        for index, (text, value) in enumerate(buttons):
            button = flat_button(panel, self.fonts, text, lambda v=value: pick(v),
                                 fg=ACCENT if index == primary_index else MUTED,
                                 bold=index == primary_index)
            if horizontal:
                button.grid(row=0, column=index, sticky="w", padx=(0, 24))
            else:
                button.grid(row=index, column=0, sticky="w", pady=2)
        top.protocol("WM_DELETE_WINDOW", lambda: pick(None))
        self._wait(top)
        return answer["value"]

    def _ask_text(self, title: str, prompt: str) -> Optional[str]:
        tk = self.tk
        top, body = self._toplevel(title)
        self._label(body, prompt, wrap=460).grid(row=0, column=0, columnspan=2, sticky="w")
        var = tk.StringVar()
        entry = self._entry(body, var, width=36)
        entry.grid(row=1, column=0, columnspan=2, sticky="w", pady=(12, 0))
        answer: dict = {"value": None}

        def ok(_event=None):
            answer["value"] = var.get()
            top.destroy()

        def cancel(_event=None):
            top.destroy()
        flat_button(body, self.fonts, "OK", ok, fg=ACCENT, bold=True).grid(row=2, column=0, sticky="w",
                                                                           pady=(12, 0))
        flat_button(body, self.fonts, "Cancel", cancel).grid(row=2, column=1, sticky="w", padx=(18, 0),
                                                              pady=(12, 0))
        entry.bind("<Return>", ok)
        entry.bind("<Escape>", cancel)
        top.protocol("WM_DELETE_WINDOW", cancel)
        entry.focus_set()
        self._wait(top)
        return answer["value"]


def _scene_text(row: SceneRow) -> str:
    label = f" {row.label}" if row.label else ""
    return f"{row.slot}{label} ({'saved' if row.saved else 'empty'})"


class TkView(View):
    """The View the controller calls. Runs on the UI thread only (MarshalledView does the hop)."""

    def __init__(self, window: InstallerWindow) -> None:
        self.w = window

    @property
    def tk(self):
        return self.w.tk

    # progress and status
    def busy(self, text):
        self.w.set_busy(text)

    def idle(self):
        self.w.set_idle()

    def step(self, text):
        self.w.step_var.set(text)

    def say(self, text):
        self._log(text, "> ", "prompt")

    def warn(self, text):
        self._log(text, "[!!] ", "warn")

    def _log(self, text: str, prefix: str = "> ", tag: str = "prompt") -> None:
        log = self.w.log
        log.configure(state="normal")
        log.insert("end", prefix, tag)
        log.insert("end", text + "\n")
        log.see("end")
        log.configure(state="disabled")

    # information
    def ports(self, ports: Sequence[PortInfo]) -> None:
        w = self.w
        w._ports = list(ports)
        w._port_values = [None] + [p.device for p in ports]
        w.port_box["values"] = [AUTOMATIC] + [f"{p.device} - {p.description}" for p in ports]
        # A remembered or chosen port that is unplugged right now is shown as Automatic for this
        # session only; the saved choice stays, so it is used again when the port comes back.
        chosen = w.session.opts.port if w.session.opts.port in w._port_values else None
        w.session.opts.port = chosen
        w.port_box.current(w._port_values.index(chosen))

    def board(self, board: BoardView) -> None:
        w = self.w
        if board.kind == "orbit12":
            w.board_label.configure(text=f"MacroPad found on {board.port}. Orbit12 is running on it.")
            w.installed_label.configure(text=f"Installed firmware: {board.firmware}")
            w.storage_label.configure(text=f"Scene storage: {board.storage}")
            return
        texts = {
            "bootloader": "The MacroPad is waiting in bootloader mode (drive RPI-RP2). Install copies Orbit12 to it.",
            "circuitpython": "A CircuitPython board is connected. Install replaces CircuitPython with Orbit12; its "
                             "files are copied to a backup first.",
            "none": "No MacroPad found. Connect it with its USB cable, then press Check again.",
            "error": board.firmware,
        }
        w.board_label.configure(text=texts.get(board.kind, texts["none"]))
        w.installed_label.configure(text="Installed firmware: none found")
        w.storage_label.configure(text="Scene storage: -")

    def releases(self, choices: ReleaseChoices) -> None:
        w = self.w
        w._choices = choices
        if choices.error:
            w.latest_label.configure(text=choices.error)
        elif choices.stable is None and choices.newest is None:
            w.latest_label.configure(text="No versions are published yet.")
        elif choices.stable is None:
            w.latest_label.configure(text=f"No stable version is published yet. Tick 'Include test (pre-release) "
                                          f"versions' to install the newest test version, {choices.newest.tag}.")
        else:
            w.latest_label.configure(text=f"Newest stable version: {choices.stable.tag}")
        w._apply_controls()

    def scenes(self, rows: Sequence[SceneRow]) -> None:
        w = self.w
        w._scene_rows = list(rows)
        w.scene_list.delete(0, "end")
        for row in rows:
            w.scene_list.insert("end", _scene_text(row))

    def outcome(self, outcome: Outcome) -> None:
        w = self.w
        colours = {"done": ACCENT, "failed": ERROR, "cancelled": MUTED}
        w._result = outcome
        w._details = outcome.details
        w.result_title.configure(text=outcome.title, fg=colours.get(outcome.status, TEXT))
        w.result_text.configure(text=outcome.text)
        w.result_next.configure(text=("What to do next: " + outcome.next_step) if outcome.next_step else "")
        if outcome.status == "done":
            self._log(f"Result: {outcome.title}", "[ok] ", "ok")
        elif outcome.status == "failed":
            self._log(f"Result: {outcome.title}", "[!!] ", "warn")
        else:
            self._log(f"Result: {outcome.title}", "> ", "prompt")

    # questions
    def confirm(self, message: str, default: bool) -> bool:
        return bool(self.w._modal(TITLE, message, [("Yes", True), ("No", False)], primary_index=0))

    def board_missing(self, message: str) -> bool:
        return bool(self.w._modal("MacroPad not found", message, [("Retry", True), ("Cancel", False)],
                                  primary_index=0))

    def physical_action(self, message: str) -> bool:
        return self.w._modal("Action needed on the board", message,
                             [("Done, continue", True), ("Cancel", False)], primary_index=0) is True

    def confirm_typed(self, word: str, message: str) -> bool:
        tk, f = self.tk, self.w.fonts
        top, body = self.w._toplevel("Type to confirm")
        self.w._label(body, message, wrap=460).grid(row=0, column=0, columnspan=2, sticky="w")
        self.w._label(body, f"Type {word} exactly as shown, then press Continue. Cancel changes nothing.",
                      colour=MUTED, wrap=460).grid(row=1, column=0, columnspan=2, sticky="w", pady=(10, 6))
        var = tk.StringVar()
        entry = self.w._entry(body, var, width=24)
        entry.grid(row=2, column=0, columnspan=2, sticky="w")
        note = self.w._label(body, "", colour=ERROR, wrap=460)
        note.grid(row=3, column=0, columnspan=2, sticky="w", pady=(4, 0))
        answer = {"value": False}

        def submit(_event=None):
            if var.get() == word:
                answer["value"] = True
                top.destroy()
            else:
                note.configure(text=f"The text does not match {word}. Nothing has been changed yet.")

        def cancel(_event=None):
            top.destroy()
        continue_btn = flat_button(body, f, "Continue", submit, fg=ACCENT, bold=True)
        continue_btn.grid(row=4, column=0, sticky="w", pady=(12, 0))
        cancel_btn = flat_button(body, f, "Cancel", cancel)
        cancel_btn.grid(row=4, column=1, sticky="w", padx=(18, 0), pady=(12, 0))
        entry.bind("<Return>", submit)
        entry.bind("<Escape>", cancel)
        top.protocol("WM_DELETE_WINDOW", cancel)
        entry.focus_set()
        self.w._wait(top)
        return bool(answer["value"])

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:
        buttons = [(text, index) for index, text in enumerate(options)]
        return self.w._modal("Choose", prompt, buttons + [("Cancel", None)])

    def ask(self, prompt: str) -> str:
        return self.w._ask_text(TITLE, prompt) or ""

    def choose_folder(self, title: str) -> Optional[str]:
        from tkinter import filedialog
        return filedialog.askdirectory(title=title, mustexist=True, parent=self.w.root) or None
