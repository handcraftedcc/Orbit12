"""Tkinter window (stdlib only). All logic is in guided.py; this module only draws and asks.

tkinter is imported inside functions, so importing this module never needs a display. Worker
threads call the view through guided.Bridge; this module pumps the bridge with after() on the UI
thread, so every widget is touched only from the UI thread.
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

AUTOMATIC = "Automatic (find my Orbit12)"
PUMP_MS = 50
TITLE = "Orbit12 installer"


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
        self._busy = False
        self._choices = ReleaseChoices(None, None)
        self._ports: List[PortInfo] = []
        self._port_values: List[Optional[str]] = []
        self._details = ""
        self._result: Optional[Outcome] = None
        self._scene_rows: List[SceneRow] = []
        self._alive = True

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
        root.title(TITLE)
        root.geometry("660x720")
        root.minsize(560, 600)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        root.report_callback_exception = self._on_tk_error
        root.columnconfigure(0, weight=1)
        root.rowconfigure(1, weight=1)

        head = ttk.Frame(root, padding=(16, 12, 16, 4))
        head.grid(row=0, column=0, sticky="ew")
        ttk.Label(head, text="Orbit12 installer", font=("TkDefaultFont", 16, "bold")).pack(anchor="w")
        ttk.Label(head, text="Install or update Orbit12 on your MacroPad. Your scenes are backed up first.",
                  wraplength=600).pack(anchor="w")
        port_row = ttk.Frame(head)
        port_row.pack(anchor="w", pady=(8, 0), fill="x")
        ttk.Label(port_row, text="MacroPad port:").pack(side="left")
        self.port_box = ttk.Combobox(port_row, state="readonly", width=44)
        self.port_box.pack(side="left", padx=6)
        self.port_box.bind("<<ComboboxSelected>>", self._on_port_chosen)
        self.ports_btn = ttk.Button(port_row, text="Refresh ports", command=self._refresh_ports)
        self.ports_btn.pack(side="left")

        self.notebook = ttk.Notebook(root)
        self.notebook.grid(row=1, column=0, sticky="nsew", padx=16, pady=8)
        self._build_install_tab()
        self._build_scenes_tab()
        self._build_modules_tab()

        bottom = ttk.Frame(root, padding=(16, 0, 16, 12))
        bottom.grid(row=3, column=0, sticky="ew")
        bottom.columnconfigure(0, weight=1)
        self.result_frame = ttk.Frame(bottom, padding=(0, 0, 0, 8))
        self.result_frame.pack(fill="x")
        self.result_title = ttk.Label(self.result_frame, font=("TkDefaultFont", 12, "bold"))
        self.result_title.pack(anchor="w")
        self.result_text = ttk.Label(self.result_frame, wraplength=600, justify="left")
        self.result_text.pack(anchor="w")
        self.result_next = ttk.Label(self.result_frame, wraplength=600, justify="left")
        self.result_next.pack(anchor="w", pady=(4, 0))
        self.copy_btn = ttk.Button(self.result_frame, text="Copy details", command=self._copy_details)
        self.copy_btn.pack(anchor="w", pady=(6, 0))
        self.status_var = tk.StringVar(value="Starting...")
        status_row = ttk.Frame(bottom)
        status_row.pack(fill="x")
        ttk.Label(status_row, textvariable=self.status_var).pack(side="left")
        self.quit_btn = ttk.Button(status_row, text="Quit", command=self._on_close)
        self.quit_btn.pack(side="right")
        self.progress = ttk.Progressbar(bottom, mode="indeterminate")
        self.progress.pack(fill="x", pady=(6, 0))

    def _build_install_tab(self) -> None:
        tk, ttk = self.tk, self.ttk
        tab = ttk.Frame(self.notebook, padding=14)
        self.notebook.add(tab, text="Install or update")
        tab.columnconfigure(0, weight=1)
        self.board_label = ttk.Label(tab, text="Looking for your MacroPad...", wraplength=600, justify="left")
        self.board_label.grid(row=0, column=0, sticky="w")
        self.installed_label = ttk.Label(tab, text="Installed firmware: -", wraplength=600, justify="left")
        self.installed_label.grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.storage_label = ttk.Label(tab, text="Scene storage: -", wraplength=600, justify="left")
        self.storage_label.grid(row=2, column=0, sticky="w")
        self.latest_label = ttk.Label(tab, text="Newest version: -", wraplength=600, justify="left")
        self.latest_label.grid(row=3, column=0, sticky="w", pady=(6, 0))
        self.include_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(tab, text="Include test (pre-release) versions", variable=self.include_var,
                        command=self._update_install_button).grid(row=4, column=0, sticky="w", pady=(6, 0))
        buttons = ttk.Frame(tab)
        buttons.grid(row=5, column=0, sticky="w", pady=(8, 0))
        self.install_btn = ttk.Button(buttons, text="Install / update", command=self._install_clicked)
        self.install_btn.pack(side="left")
        self.check_btn = ttk.Button(buttons, text="Check again", command=self._refresh)
        self.check_btn.pack(side="left", padx=8)
        self.step_var = tk.StringVar(value="")
        ttk.Label(tab, textvariable=self.step_var, wraplength=600).grid(row=6, column=0, sticky="w", pady=(10, 0))
        log_frame = ttk.Frame(tab)
        log_frame.grid(row=7, column=0, sticky="nsew", pady=(8, 0))
        tab.rowconfigure(7, weight=1)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log = tk.Text(log_frame, height=8, wrap="word", state="disabled")
        self.log.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(log_frame, command=self.log.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log.configure(yscrollcommand=scroll.set)

    def _build_scenes_tab(self) -> None:
        tk, ttk = self.tk, self.ttk
        tab = ttk.Frame(self.notebook, padding=14)
        self.notebook.add(tab, text="Scenes")
        tab.columnconfigure(0, weight=1)
        ttk.Label(tab, text="Scenes on the MacroPad. Backups are saved next to the program. "
                            "Restoring replaces what is on the board, and asks you first.",
                  wraplength=600, justify="left").grid(row=0, column=0, sticky="w")
        self.scene_list = tk.Listbox(tab, height=12)
        self.scene_list.grid(row=1, column=0, sticky="nsew", pady=8)
        tab.rowconfigure(1, weight=1)
        grid = ttk.Frame(tab)
        grid.grid(row=2, column=0, sticky="w")
        self.scene_buttons = [
            ttk.Button(grid, text="Show scenes on the board", command=self._scenes_show),
            ttk.Button(grid, text="Back up all scenes", command=lambda: self._start(self.session.backup_scenes, None)),
            ttk.Button(grid, text="Back up one scene...", command=self._scene_backup_one),
            ttk.Button(grid, text="Restore all from a backup...", command=lambda: self._start(self.session.restore_scenes, False)),
            ttk.Button(grid, text="Restore one scene from a backup...", command=lambda: self._start(self.session.restore_scenes, True)),
        ]
        for index, button in enumerate(self.scene_buttons):
            button.grid(row=index // 2, column=index % 2, sticky="ew", padx=4, pady=3)

    def _build_modules_tab(self) -> None:
        tab = self.ttk.Frame(self.notebook, padding=14)
        self.notebook.add(tab, text="Modules")
        self.ttk.Label(tab, text="Modules: coming soon.\n\nThis tab will let you switch modules on and off "
                                 "once the firmware supports it. Nothing here changes the board.",
                       wraplength=560, justify="left").pack(anchor="w")

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

    def _update_install_button(self) -> None:
        self._apply_controls()

    def _apply_controls(self) -> None:
        state = "disabled" if self._busy else "normal"
        for widget in (self.check_btn, self.ports_btn, *self.scene_buttons):
            widget.configure(state=state)
        self.port_box.configure(state="disabled" if self._busy else "readonly")
        release = guided.pick_release(self._choices, self.include_var.get())
        if release is not None and not self._busy:
            self.install_btn.configure(state="normal", text=f"Install / update to {release.tag}")
        else:
            self.install_btn.configure(state="disabled", text="Install / update")
        if self._busy:
            self.progress.start(12)
        else:
            self.progress.stop()

    def _copy_details(self) -> None:
        self.root.clipboard_clear()
        self.root.clipboard_append(self._details)
        self.status_var.set("Details copied. Paste them into your message to support.")

    def _on_close(self) -> None:
        from tkinter import messagebox
        if self._busy:
            messagebox.showinfo(
                TITLE, "The installer is still working. Closing now could leave the board half-written. "
                       "Wait until it says it is finished, then close.", parent=self.root)
            return
        self._alive = False
        self.root.destroy()

    def _on_tk_error(self, exc_type, value, _tb) -> None:
        from tkinter import messagebox
        messagebox.showerror(
            TITLE, "Something unexpected happened in the window. Close the installer and open it again. "
                   "If it keeps happening, tell support.", parent=self.root)

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
        self._log(text)

    def warn(self, text):
        self._log("WARNING: " + text)

    def _log(self, text: str) -> None:
        self.w.log.configure(state="normal")
        self.w.log.insert("end", text + "\n")
        self.w.log.see("end")
        self.w.log.configure(state="disabled")

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
        colours = {"done": "#1b7f3b", "failed": "#b00020", "cancelled": "#555555"}
        w._result = outcome
        w._details = outcome.details
        w.result_title.configure(text=outcome.title, foreground=colours.get(outcome.status, "#000000"))
        w.result_text.configure(text=outcome.text)
        w.result_next.configure(text=("What to do next: " + outcome.next_step) if outcome.next_step else "")
        self._log(f"Result: {outcome.title}")

    # questions
    def confirm(self, message: str, default: bool) -> bool:
        return bool(self.tk_messagebox().askyesno(TITLE, message, parent=self.w.root))

    def board_missing(self, message: str) -> bool:
        return bool(self.tk_messagebox().askretrycancel("MacroPad not found", message, parent=self.w.root))

    def tk_messagebox(self):
        from tkinter import messagebox
        return messagebox

    def physical_action(self, message: str) -> bool:
        return self._modal("Action needed on the board", message,
                           [("Done, continue", True), ("Cancel", False)]) is True

    def confirm_typed(self, word: str, message: str) -> bool:
        tk, ttk = self.tk, self.w.ttk
        top, body = self._toplevel("Type to confirm")
        ttk.Label(body, text=message, wraplength=460, justify="left").grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Label(body, text=f"Type {word} exactly as shown, then press Continue. Cancel changes nothing.",
                  wraplength=460, justify="left").grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 4))
        var = tk.StringVar()
        entry = ttk.Entry(body, textvariable=var, width=24)
        entry.grid(row=2, column=0, columnspan=2, sticky="w")
        note = ttk.Label(body, text="", foreground="#b00020")
        note.grid(row=3, column=0, columnspan=2, sticky="w")
        answer = {"value": False}

        def submit(_event=None):
            if var.get() == word:
                answer["value"] = True
                top.destroy()
            else:
                note.configure(text=f"The text does not match {word}. Nothing has been changed yet.")

        def cancel():
            top.destroy()
        ttk.Button(body, text="Continue", command=submit).grid(row=4, column=0, sticky="e", pady=(10, 0))
        ttk.Button(body, text="Cancel", command=cancel).grid(row=4, column=1, sticky="w", padx=6, pady=(10, 0))
        entry.bind("<Return>", submit)
        top.protocol("WM_DELETE_WINDOW", cancel)
        entry.focus_set()
        self._wait(top)
        return bool(answer["value"])

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:
        buttons = [(text, index) for index, text in enumerate(options)]
        return self._modal("Choose", prompt, buttons + [("Cancel", None)])

    def ask(self, prompt: str) -> str:
        from tkinter import simpledialog
        value = simpledialog.askstring(TITLE, prompt, parent=self.w.root)
        return value or ""

    def choose_folder(self, title: str) -> Optional[str]:
        from tkinter import filedialog
        return filedialog.askdirectory(title=title, mustexist=True, parent=self.w.root) or None

    # dialog helpers
    def _toplevel(self, title: str):
        tk, ttk = self.tk, self.w.ttk
        top = tk.Toplevel(self.w.root)
        top.title(title)
        top.transient(self.w.root)
        top.resizable(False, False)
        body = ttk.Frame(top, padding=16)
        body.grid(row=0, column=0)
        return top, body

    def _wait(self, top) -> None:
        top.grab_set()
        top.wait_window()

    def _modal(self, title: str, message: str, buttons):
        top, body = self._toplevel(title)
        self.w.ttk.Label(body, text=message, wraplength=460, justify="left").grid(row=0, column=0, sticky="w")
        answer = {"value": None}

        def pick(value):
            answer["value"] = value
            top.destroy()
        for index, (text, value) in enumerate(buttons):
            self.w.ttk.Button(body, text=text, command=lambda v=value: pick(v)).grid(
                row=1, column=0, sticky="w", pady=3)
        top.protocol("WM_DELETE_WINDOW", lambda: pick(None))
        self._wait(top)
        return answer["value"]
