#!/usr/bin/env python3
"""Build and install Orbit12 DeviceFiles to CIRCUITPY.

Workflow:
1. Copy DeviceFiles -> build folder
2. Compile non-entry .py files to .mpy
3. Deploy build to CIRCUITPY with rsync, preserving userdata
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable, List, Sequence


def should_preserve_py(relative_path: str) -> bool:
    """Return True when a .py file must remain as source on-device."""
    rel = relative_path.replace("\\", "/")

    if rel == "code.py":
        return True

    parts = rel.split("/")
    # apps/<AppName>/code.py
    if len(parts) == 3 and parts[0] == "apps" and parts[2] == "code.py":
        return True

    # apps/<AppName>/<AppName>.py (fallback app entry)
    if len(parts) == 3 and parts[0] == "apps" and parts[2] == (parts[1] + ".py"):
        return True

    return False


def _iter_python_files(root: pathlib.Path) -> Iterable[pathlib.Path]:
    for path in root.rglob("*.py"):
        if path.is_file():
            yield path


def resolve_mpy_cross(mpy_cross: str | None) -> List[str]:
    """Resolve mpy-cross command list."""
    if mpy_cross:
        return [mpy_cross]

    found = shutil.which("mpy-cross")
    if found:
        return [found]

    raise RuntimeError(
        "mpy-cross not found. Install it or pass --mpy-cross /path/to/mpy-cross"
    )


def is_circuitpython_mpy_file(path: pathlib.Path) -> bool:
    """Return True when an .mpy file appears to be CircuitPython-flavored.

    CircuitPython bundle .mpy files in this project start with byte 'C' (0x43),
    while generic MicroPython mpy-cross output starts with 'M' (0x4D).
    """
    with path.open("rb") as handle:
        header = handle.read(1)
    return header == b"C"


def verify_mpy_cross_compatibility(mpy_cross_cmd: Sequence[str]) -> bool:
    """Compile a tiny probe and verify the output looks CircuitPython-compatible."""
    with tempfile.TemporaryDirectory() as td:
        td_path = pathlib.Path(td)
        probe_py = td_path / "probe.py"
        probe_mpy = td_path / "probe.mpy"
        probe_py.write_text("X = 1\n", encoding="utf-8")
        compile_file(mpy_cross_cmd, probe_py, probe_mpy)
        return is_circuitpython_mpy_file(probe_mpy)


def compile_file(mpy_cross_cmd: Sequence[str], py_path: pathlib.Path, mpy_path: pathlib.Path) -> None:
    cmd = list(mpy_cross_cmd) + ["-o", str(mpy_path), str(py_path)]
    subprocess.run(cmd, check=True)


def build_artifact(
    source_root: pathlib.Path,
    build_root: pathlib.Path,
    mpy_cross_cmd: Sequence[str] | None,
) -> None:
    """Create build folder and compile non-entry Python files to .mpy."""
    if build_root.exists():
        shutil.rmtree(build_root)
    shutil.copytree(source_root, build_root)

    if mpy_cross_cmd is None:
        return

    for py_path in _iter_python_files(build_root):
        rel = py_path.relative_to(build_root).as_posix()
        if rel.startswith("userdata/"):
            continue
        if should_preserve_py(rel):
            continue

        mpy_path = py_path.with_suffix(".mpy")
        compile_file(mpy_cross_cmd, py_path, mpy_path)
        py_path.unlink()


def detect_existing_adafruit_paths(build_root: pathlib.Path, device_path: pathlib.Path) -> List[str]:
    """Return rsync exclude paths for Adafruit libs already present on device.

    Only top-level entries inside `lib/` that start with `adafruit_` are considered.
    """
    excludes: List[str] = []
    build_lib = build_root / "lib"
    device_lib = device_path / "lib"
    if not build_lib.exists() or not device_lib.exists():
        return excludes

    for item in sorted(build_lib.iterdir(), key=lambda p: p.name):
        name = item.name
        if not name.startswith("adafruit_"):
            continue
        device_item = device_lib / name
        if not device_item.exists():
            continue
        if item.is_dir():
            excludes.append("lib/" + name + "/")
        else:
            excludes.append("lib/" + name)
    return excludes


def build_rsync_command(
    build_root: str,
    device_path: str,
    dry_run: bool = False,
    extra_excludes: Sequence[str] | None = None,
) -> List[str]:
    cmd = [
        "rsync",
        "-rv",
        "--delete",
        "--exclude",
        "userdata/",
        "--exclude",
        ".DS_Store",
        "--exclude",
        "._*",
        "--exclude",
        ".Trashes",
        "--exclude",
        ".Trash-*",
        "--exclude",
        ".fseventsd/",
        "--exclude",
        ".Spotlight-V100/",
        "--exclude",
        ".metadata_never_index",
        "--exclude",
        "LOST.DIR/",
        "--exclude",
        "sd/",
        "--exclude",
        "boot_out.txt",
    ]
    if dry_run:
        cmd.append("--dry-run")

    for extra in extra_excludes or ():
        cmd.extend(["--exclude", str(extra)])

    src = str(pathlib.Path(build_root)) + "/"
    dst = str(pathlib.Path(device_path)) + "/"
    cmd.extend([src, dst])
    return cmd


def deploy_build(
    build_root: pathlib.Path,
    device_path: pathlib.Path,
    dry_run: bool = False,
    extra_excludes: Sequence[str] | None = None,
) -> None:
    cmd = build_rsync_command(
        str(build_root),
        str(device_path),
        dry_run=dry_run,
        extra_excludes=extra_excludes,
    )
    subprocess.run(cmd, check=True)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build and deploy Orbit12 to CIRCUITPY")
    parser.add_argument("--source", default="DeviceFiles", help="Source device mirror folder")
    parser.add_argument("--build", default="build", help="Output build folder")
    parser.add_argument("--device", default="/Volumes/CIRCUITPY", help="Mounted CIRCUITPY path")
    parser.add_argument("--mpy-cross", default=None, help="Path to mpy-cross binary")
    parser.add_argument("--build-only", action="store_true", help="Build only, do not deploy")
    parser.add_argument("--dry-run", action="store_true", help="Show deploy changes without writing")
    parser.add_argument(
        "--no-compile",
        action="store_true",
        help="Skip .py -> .mpy compilation (useful fallback if mpy-cross is incompatible)",
    )
    parser.add_argument(
        "--reinstall-adafruit-libraries",
        "--reinstalladafruitlibraries",
        dest="reinstall_adafruit_libraries",
        action="store_true",
        help="Reinstall Adafruit libraries even if matching packages already exist on device",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])

    source_root = pathlib.Path(args.source).resolve()
    build_root = pathlib.Path(args.build).resolve()
    device_path = pathlib.Path(args.device).resolve()

    if not source_root.exists():
        print("Source folder not found:", source_root)
        return 2

    mpy_cross_cmd: Sequence[str] | None = None
    if not args.no_compile:
        try:
            mpy_cross_cmd = resolve_mpy_cross(args.mpy_cross)
        except RuntimeError as err:
            print(str(err))
            return 2

        if not verify_mpy_cross_compatibility(mpy_cross_cmd):
            print(
                "mpy-cross output is not CircuitPython-compatible for this workflow.\n"
                "Use a CircuitPython-compatible mpy compiler, or rerun with --no-compile."
            )
            return 2

    print("[1/2] Building artifact:", build_root)
    build_artifact(source_root, build_root, mpy_cross_cmd)

    if args.build_only:
        print("Build complete (build-only).")
        return 0

    if not device_path.exists():
        print("Device path not found:", device_path)
        return 2

    extra_excludes: List[str] = []
    if not args.reinstall_adafruit_libraries:
        extra_excludes = detect_existing_adafruit_paths(build_root, device_path)
        if extra_excludes:
            print(
                "Skipping",
                len(extra_excludes),
                "existing Adafruit package(s). Use --reinstall-adafruit-libraries to force reinstall.",
            )

    print("[2/2] Deploying to device:", device_path)
    deploy_build(
        build_root,
        device_path,
        dry_run=args.dry_run,
        extra_excludes=extra_excludes,
    )
    print("Install complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
