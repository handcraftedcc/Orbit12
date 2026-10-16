#!/usr/bin/env python3
"""Builds the one-file installer with PyInstaller for the current platform.

    pip install pyinstaller pyserial
    python tools/installer/build_exe.py [--no-smoke]

Outputs: build/installer/dist/orbit12-installer-<windows|macos|linux>[.exe]
Work and spec files stay under build/installer/ (ignored by git). Nothing is published.
The CI workflow runs this same script on each runner; it is not changed by this file.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD_ROOT = os.path.join(REPO_ROOT, "build", "installer")
ENTRY = os.path.join(HERE, "entry.py")


def platform_name() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    raise SystemExit(f"unsupported platform: {sys.platform}")


def output_name() -> str:
    return f"orbit12-installer-{platform_name()}" + (".exe" if sys.platform == "win32" else "")


def _require(module: str, hint: str) -> None:
    if importlib.util.find_spec(module) is None:
        raise SystemExit(f"{module} is missing. Install the build tools with: {hint}")


def build_command(name: str) -> list:
    dist = os.path.join(BUILD_ROOT, "dist")
    work = os.path.join(BUILD_ROOT, "work")
    spec = os.path.join(BUILD_ROOT, "spec")
    return [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm", "--clean", "--onefile", "--console",
        "--name", name,
        "--distpath", dist, "--workpath", work, "--specpath", spec,
        "--paths", HERE,
        "--hidden-import", "serial",
        "--hidden-import", "serial.tools.list_ports",
        ENTRY,
    ]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-smoke", action="store_true", help="skip running the built program with --version")
    parser.add_argument("--print-command", action="store_true", help="print the PyInstaller command and stop")
    args = parser.parse_args(argv)

    name = output_name().rsplit(".exe", 1)[0] if sys.platform == "win32" else output_name()
    command = build_command(name)
    if args.print_command:
        print(" ".join(f'"{part}"' if " " in part else part for part in command))
        return 0

    _require("PyInstaller", "pip install pyinstaller pyserial")
    _require("serial", "pip install pyinstaller pyserial")
    os.makedirs(os.path.join(BUILD_ROOT, "dist"), exist_ok=True)
    result = subprocess.run(command, cwd=BUILD_ROOT)
    if result.returncode != 0:
        print("PyInstaller failed; see the output above.", file=sys.stderr)
        return result.returncode

    built = os.path.join(BUILD_ROOT, "dist", output_name())
    if not os.path.isfile(built):
        print(f"expected {built} but it was not produced", file=sys.stderr)
        return 1
    print(f"built {built} ({os.path.getsize(built)} bytes)")
    if not args.no_smoke:
        smoke = subprocess.run([built, "--version"], capture_output=True, text=True, timeout=120)
        if smoke.returncode != 0:
            print(f"smoke test failed ({smoke.returncode}): {smoke.stderr.strip()}", file=sys.stderr)
            return 1
        print("smoke test ok: " + smoke.stdout.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
