#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools import build_mpy


BOARD_ROOT = Path("/Volumes/CIRCUITPY")


class CurrentFilePlan:
    def __init__(
        self,
        source_file,
        build_output,
        board_output,
        compile_to_mpy,
        stale_board_source=None,
        source_name=None,
    ):
        self.source_file = source_file
        self.build_output = build_output
        self.board_output = board_output
        self.compile_to_mpy = compile_to_mpy
        self.stale_board_source = stale_board_source
        self.source_name = source_name


def is_relative_to(path, parent):
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def build_plan(source_file, project_root, board_root=BOARD_ROOT):
    source_file = Path(source_file).resolve()
    project_root = Path(project_root).resolve()
    board_root = Path(board_root)

    src_root = project_root / "src"
    app_source_root = project_root / "src" / "apps" / "Orion"
    app_output_root = project_root / "src_mpy" / "apps" / "Orion"
    lib_source_root = project_root / "src" / "lib"
    lib_output_root = project_root / "src_mpy" / "lib"

    if source_file.suffix != ".py":
        raise ValueError("Current file must be a .py file")

    if is_relative_to(source_file, app_source_root):
        compiled_file = build_mpy.output_path(source_file, app_source_root, app_output_root)
        relative_path = source_file.relative_to(app_source_root)
        board_output = board_root / "apps" / "Orion" / relative_path.with_suffix(".mpy")
        stale_source = board_root / "apps" / "Orion" / relative_path
        return CurrentFilePlan(source_file, compiled_file, board_output, True, stale_source, relative_path)

    if is_relative_to(source_file, lib_source_root):
        compiled_file = build_mpy.output_path(source_file, lib_source_root, lib_output_root)
        relative_path = source_file.relative_to(lib_source_root)
        board_output = board_root / "lib" / relative_path.with_suffix(".mpy")
        stale_source = board_root / "lib" / relative_path
        return CurrentFilePlan(source_file, compiled_file, board_output, True, stale_source, relative_path)

    if is_relative_to(source_file, src_root):
        relative_path = source_file.relative_to(src_root)
        return CurrentFilePlan(source_file, source_file, board_root / relative_path, False)

    raise ValueError("Current file must be inside src")


def sync_file(source_file, board_output):
    board_output.parent.mkdir(parents=True, exist_ok=True)
    return subprocess.run(
        [
            "rsync",
            "-avh",
            "--itemize-changes",
            "--progress",
            str(source_file),
            str(board_output),
        ]
    ).returncode


def remove_stale_source(path):
    if path is not None and path.exists():
        path.unlink()


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Build and sync the current Orion/CircuitPython file.")
    parser.add_argument("file", help="Current PyCharm file path, usually $FilePath$")
    parser.add_argument(
        "--board",
        default=str(BOARD_ROOT),
        help="CircuitPython board root. Default: /Volumes/CIRCUITPY",
    )
    parser.add_argument(
        "--mpy-cross",
        default=str(build_mpy.default_mpy_cross()),
        help="Path to mpy-cross. Can also be set with MPY_CROSS.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Recompile even if the .mpy output is newer.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)

    try:
        plan = build_plan(Path(args.file), build_mpy.PROJECT_ROOT, Path(args.board))
    except ValueError as error:
        print(error)
        return 2

    if plan.compile_to_mpy:
        if not build_mpy.validate_mpy_cross(Path(args.mpy_cross)):
            return 2
        if build_mpy.should_compile(plan.source_file, plan.build_output, args.force):
            print("MPY:", plan.source_file, "->", plan.build_output)
            result = build_mpy.compile_file(
                Path(args.mpy_cross),
                plan.source_file,
                plan.build_output,
                source_name=plan.source_name,
            )
            if result != 0:
                return result
        else:
            print("MPY: up to date", plan.build_output)
        remove_stale_source(plan.stale_board_source)

    print("SYNC:", plan.build_output, "->", plan.board_output)
    return sync_file(plan.build_output, plan.board_output)


if __name__ == "__main__":
    raise SystemExit(main())
