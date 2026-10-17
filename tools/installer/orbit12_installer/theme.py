"""Dark, flat, terminal look for the tkinter window (stdlib only).

Palette and typography follow the user's My-Focus-Assistant app. Everything here is visual: no
installer logic lives in this module. tkinter is imported inside functions so importing it never
needs a display.
"""

from __future__ import annotations

import sys
from typing import Dict

BG = "#0c1014"        # window and every surface
TEXT = "#d3ebe9"      # body text
MUTED = "#99d1ce"     # secondary text and inactive buttons
ACCENT = "#2aa889"    # primary actions, success, the next step
INFO = "#33859e"      # selection and information
DIM = "#599cab"       # done or disabled
RULE = "#245361"      # 1px dividers, borders
SELECT = "#0a3749"    # selected and hovered rows
ERROR = "#e06c75"     # failures and warnings

MONO_FAMILIES = ("Consolas", "Cascadia Mono", "Courier New")
BASE_SIZE = 10  # points; about 13px at the system DPI


def enable_dpi_awareness() -> None:
    """Stops Windows from bitmap-scaling the window blurry on high-DPI screens. Must run before Tk()."""
    if sys.platform != "win32":
        return
    try:
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)  # PROCESS_SYSTEM_DPI_AWARE
        except (AttributeError, OSError):
            ctypes.windll.user32.SetProcessDPIAware()
    except (AttributeError, OSError, ImportError):
        pass


def dark_title_bar(window) -> None:
    """Makes the Windows title bar (caption, minimise/close buttons) dark like the window body.

    Uses DWM attributes: immersive dark mode (Windows 10 1809+ / 11) and, on Windows 11, an explicit
    caption and text colour. Silently does nothing elsewhere or when the calls are unavailable.
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
        window.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(window.winfo_id()) or window.winfo_id()
        dwm = ctypes.windll.dwmapi

        def put(attribute: int, value: int) -> int:
            data = ctypes.c_int(value)
            return dwm.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(data), ctypes.sizeof(data))

        if put(20, 1) != 0:  # DWMWA_USE_IMMERSIVE_DARK_MODE (19 on early Windows 10 builds)
            put(19, 1)

        def colorref(hex_colour: str) -> int:  # COLORREF is 0x00BBGGRR
            r, g, b = (int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))
            return (b << 16) | (g << 8) | r

        put(35, colorref(BG))    # DWMWA_CAPTION_COLOR (Windows 11)
        put(36, colorref(TEXT))  # DWMWA_TEXT_COLOR (Windows 11)
        put(34, colorref(RULE))  # DWMWA_BORDER_COLOR (Windows 11)
    except (AttributeError, OSError, ImportError, ValueError):
        pass


def mono_family(root) -> str:
    """The first installed monospace face from the preference list, else Tk's own fixed font."""
    import tkinter.font as tkfont
    installed = set(tkfont.families(root))
    for name in MONO_FAMILIES:
        if name in installed:
            return name
    return tkfont.nametofont("TkFixedFont", root=root).actual("family")


def apply_theme(root) -> Dict[str, object]:
    """Styles the root window and the standard named fonts; returns the font set used by widgets."""
    import tkinter.font as tkfont
    from tkinter import ttk

    family = mono_family(root)
    for name in ("TkDefaultFont", "TkTextFont", "TkFixedFont", "TkMenuFont"):
        tkfont.nametofont(name, root=root).configure(family=family, size=BASE_SIZE)

    root.configure(background=BG)
    # Dropdown lists of ttk comboboxes are plain Tk listboxes; colour them through the option database.
    root.option_add("*TCombobox*Listbox.background", BG)
    root.option_add("*TCombobox*Listbox.foreground", TEXT)
    root.option_add("*TCombobox*Listbox.selectBackground", SELECT)
    root.option_add("*TCombobox*Listbox.selectForeground", ACCENT)

    style = ttk.Style(root)
    if "clam" in style.theme_names():
        style.theme_use("clam")  # the only ttk theme that accepts the colours below on Windows
    style.configure("TCombobox", fieldbackground=BG, background=BG, foreground=TEXT, arrowcolor=MUTED,
                    bordercolor=RULE, lightcolor=RULE, darkcolor=RULE, selectbackground=SELECT,
                    selectforeground=TEXT, insertcolor=TEXT, padding=(6, 3))
    style.map("TCombobox", fieldbackground=[("readonly", BG), ("disabled", BG)],
              foreground=[("readonly", TEXT), ("disabled", DIM)],
              background=[("active", SELECT), ("readonly", BG)],
              arrowcolor=[("active", ACCENT), ("disabled", DIM)],
              bordercolor=[("focus", INFO)], selectbackground=[("readonly", BG)],
              selectforeground=[("readonly", TEXT)])
    style.configure("Vertical.TScrollbar", background=RULE, troughcolor=BG, bordercolor=BG,
                    lightcolor=BG, darkcolor=BG, arrowcolor=MUTED, relief="flat")
    style.map("Vertical.TScrollbar", background=[("active", INFO)])
    # The notebook keeps its page-switching behaviour, but its native tabs are removed: the tab
    # strip is drawn as text labels above it (see gui.py).
    style.layout("Hidden.TNotebook.Tab", [])
    style.layout("Hidden.TNotebook", [("Notebook.client", {"sticky": "nswe"})])
    style.configure("Hidden.TNotebook", background=BG, borderwidth=0, tabmargins=0)

    return {
        "body": tkfont.Font(root=root, family=family, size=BASE_SIZE),
        "bold": tkfont.Font(root=root, family=family, size=BASE_SIZE, weight="bold"),
        "body_u": tkfont.Font(root=root, family=family, size=BASE_SIZE, underline=True),
        "bold_u": tkfont.Font(root=root, family=family, size=BASE_SIZE, weight="bold", underline=True),
        "title": tkfont.Font(root=root, family=family, size=BASE_SIZE + 2, weight="bold"),
        "mono": tkfont.Font(root=root, family=family, size=BASE_SIZE),
    }


def flat_button(parent, fonts: Dict[str, object], text: str, command, fg: str = MUTED, bold: bool = False):
    """A flat text button: no chrome, muted until hovered, then accent-coloured and underlined.

    Disabled buttons use DIM through disabledforeground and never react to the pointer.
    """
    import tkinter as tk
    base = fonts["bold" if bold else "body"]
    under = fonts["bold_u" if bold else "body_u"]
    button = tk.Button(parent, text=text, command=command, font=base, anchor="w", justify="left",
                       background=BG, activebackground=BG, activeforeground=ACCENT, foreground=fg,
                       disabledforeground=DIM, relief="flat", overrelief="flat", bd=0,
                       highlightthickness=0, padx=0, pady=2, cursor="hand2")
    button._flat_fg = fg  # resting colour, restored when the pointer leaves

    def enter(_event=None):
        if str(button.cget("state")) != "disabled":
            button.configure(font=under, foreground=ACCENT)

    def leave(_event=None):
        button.configure(font=base, foreground=button._flat_fg)

    button.bind("<Enter>", enter)
    button.bind("<Leave>", leave)
    return button


def set_flat_colour(button, fg: str) -> None:
    """Changes the resting colour of a flat button (used for the active tab)."""
    button._flat_fg = fg
    button.configure(foreground=fg)


def rule(parent, side: str = "top", pad=(0, 0)):
    """A 1px horizontal divider in the border colour (packed; pass side="bottom" for footer rules)."""
    import tkinter as tk
    line = tk.Frame(parent, background=RULE, height=1, bd=0, highlightthickness=0)
    line.pack(side=side, fill="x", pady=pad)
    return line
