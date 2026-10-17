"""PyInstaller entry script. Kept outside the package so the frozen program starts the CLI directly.

The Windows build is windowed (no console window). When it is run with arguments (the CLI), it
attaches to the console that started it, so --help, --version and the other commands print there.
A double-click passes no arguments and opens the window.
"""

import os
import sys

from orbit12_installer.cli import main


def _console_for_windowed_build() -> None:
    if sys.platform != "win32" or not getattr(sys, "frozen", False):
        return
    if sys.stdout is not None and sys.stderr is not None and sys.stdin is not None:
        return  # streams already exist (console or a pipe): leave them alone
    if not sys.argv[1:]:
        # The window needs no output; give the standard streams a sink so nothing can crash on None.
        if sys.stdout is None:
            sys.stdout = open(os.devnull, "w", encoding="utf-8")
        if sys.stderr is None:
            sys.stderr = open(os.devnull, "w", encoding="utf-8")
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        if not kernel32.AttachConsole(-1):  # ATTACH_PARENT_PROCESS: the console that ran us
            kernel32.AllocConsole()  # started without a console (for example from a shortcut)
        if sys.stdout is None:
            sys.stdout = open("CONOUT$", "w", encoding="utf-8", buffering=1)
        if sys.stderr is None:
            sys.stderr = open("CONOUT$", "w", encoding="utf-8", buffering=1)
        if sys.stdin is None:
            sys.stdin = open("CONIN$", "r", encoding="utf-8")
    except Exception:  # no console available: output is dropped, and the CLI reports exit codes
        if sys.stdout is None:
            sys.stdout = open(os.devnull, "w", encoding="utf-8")
        if sys.stderr is None:
            sys.stderr = open(os.devnull, "w", encoding="utf-8")
        if sys.stdin is None:
            sys.stdin = open(os.devnull, "r", encoding="utf-8")


if __name__ == "__main__":
    _console_for_windowed_build()
    sys.exit(main())
