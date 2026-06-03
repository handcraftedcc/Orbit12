#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APP_SOURCE_ROOT = PROJECT_ROOT / "src" / "apps" / "Orion"
DEFAULT_APP_OUTPUT_ROOT = PROJECT_ROOT / "src_mpy" / "apps" / "Orion"
DEFAULT_LIB_SOURCE_ROOT = PROJECT_ROOT / "src" / "lib"
DEFAULT_LIB_OUTPUT_ROOT = PROJECT_ROOT / "src_mpy" / "lib"
IGNORE_NAMES = (".DS_Store",)
IGNORE_PREFIXES = ("._",)
IGNORE_DIRS = ("__pycache__",)


def default_mpy_cross():
    env_path = os.environ.get("MPY_CROSS")
    if env_path:
        return Path(env_path)

    stable_path = PROJECT_ROOT / "tools" / "circuitpython-mpy-cross" / "mpy-cross"
    if stable_path.exists():
        return stable_path

    downloaded_paths = sorted((PROJECT_ROOT / "mpy-cross").glob("mpy-cross-*"), reverse=True)
    if downloaded_paths:
        return downloaded_paths[0]

    local_path = PROJECT_ROOT / ".venv" / "bin" / "mpy-cross"
    if local_path.exists():
        return local_path

    return Path("mpy-cross")


def output_path(source_file, source_root, output_root):
    relative_path = source_file.relative_to(source_root)
    return output_root / relative_path.with_suffix(".mpy")


def copy_output_path(source_file, source_root, output_root):
    return output_root / source_file.relative_to(source_root)


def ignored_file(path):
    if path.name in IGNORE_NAMES:
        return True
    for prefix in IGNORE_PREFIXES:
        if path.name.startswith(prefix):
            return True
    for parent in path.parents:
        if parent.name in IGNORE_DIRS:
            return True
    return False


def source_files(source_root):
    return sorted(path for path in source_root.rglob("*.py") if path.is_file())


def copy_source_files(source_root):
    return sorted(
        path
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix != ".py" and not ignored_file(path)
    )


def should_compile(source_file, output_file, force=False):
    if force:
        return True
    if not output_file.exists():
        return True
    return source_file.stat().st_mtime > output_file.stat().st_mtime


def collect_compile_jobs(source_root, output_root, force=False):
    jobs = []
    for source_file in source_files(source_root):
        compiled_file = output_path(source_file, source_root, output_root)
        if should_compile(source_file, compiled_file, force):
            jobs.append((source_file, compiled_file))
    return jobs


def should_copy(source_file, output_file, force=False):
    if force:
        return True
    if not output_file.exists():
        return True
    source_stat = source_file.stat()
    output_stat = output_file.stat()
    return source_stat.st_size != output_stat.st_size or source_stat.st_mtime > output_stat.st_mtime


def collect_copy_jobs(source_root, output_root, force=False):
    jobs = []
    for source_file in copy_source_files(source_root):
        copied_file = copy_output_path(source_file, source_root, output_root)
        if should_copy(source_file, copied_file, force):
            jobs.append((source_file, copied_file))
    return jobs


def run_command(command):
    return subprocess.run(command).returncode


def mpy_cross_version(mpy_cross):
    try:
        result = subprocess.run(
            [str(mpy_cross), "--version"],
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    version_text = (result.stdout or "") + (result.stderr or "")
    return version_text.strip()


def is_circuitpython_mpy_cross(mpy_cross):
    version_text = mpy_cross_version(mpy_cross)
    return version_text is not None and "CircuitPython" in version_text


def validate_mpy_cross(mpy_cross):
    version_text = mpy_cross_version(mpy_cross)
    if version_text is None:
        print("mpy-cross not found:", mpy_cross)
        return False
    if "CircuitPython" not in version_text:
        print("Incompatible mpy-cross:", version_text)
        print("Use the CircuitPython mpy-cross matching the board firmware, not the PyPI/MicroPython one.")
        return False
    return True


def compile_file(mpy_cross, source_file, output_file, runner=run_command, source_name=None):
    output_file.parent.mkdir(parents=True, exist_ok=True)
    command = [str(mpy_cross), "-O3", "-o", str(output_file)]
    if source_name is not None:
        command.extend(["-s", str(source_name)])
    command.append(str(source_file))
    return runner(command)


def copy_file(source_file, output_file):
    output_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_file, output_file)


def build_tree(label, source_root, output_root, mpy_cross, force=False):
    if not source_root.exists():
        print(label + ": source directory not found:", source_root)
        return 2

    compile_jobs = collect_compile_jobs(source_root, output_root, force)
    copy_jobs = collect_copy_jobs(source_root, output_root, force)

    failed = 0
    for source_file, compiled_file in compile_jobs:
        relative_source = source_file.relative_to(source_root)
        print(label + " MPY:", relative_source, "->", compiled_file)
        result = compile_file(mpy_cross, source_file, compiled_file, source_name=relative_source)
        if result != 0:
            failed += 1

    for source_file, copied_file in copy_jobs:
        print(label + " COPY:", source_file.relative_to(source_root), "->", copied_file)
        copy_file(source_file, copied_file)

    if failed:
        print(label + " build failed:", failed, "file(s)")
        return 1

    print(
        label + " build:",
        len(compile_jobs),
        "compiled,",
        len(copy_jobs),
        "copied",
    )
    return 0


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Build a CircuitPython src_mpy tree with compiled Orion app and lib files."
    )
    parser.add_argument(
        "--source",
        default=None,
        help="Optional source directory to build instead of the default app+lib build.",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output directory for --source.",
    )
    parser.add_argument(
        "--mpy-cross",
        default=str(default_mpy_cross()),
        help="Path to mpy-cross. Can also be set with MPY_CROSS.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Recompile every .py file, ignoring timestamps.",
    )
    parser.add_argument(
        "--no-lib",
        action="store_true",
        help="Skip the lib/ build tree. Only build the app.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    mpy_cross = Path(args.mpy_cross)

    if not validate_mpy_cross(mpy_cross):
        return 2

    if args.source or args.output:
        if not args.source or not args.output:
            print("--source and --output must be used together")
            return 2

        result = build_tree(
            "CUSTOM",
            Path(args.source).resolve(),
            Path(args.output).resolve(),
            mpy_cross,
            args.force,
        )
        if result == 0:
            print("MPY build complete")
        return result

    app_result = build_tree(
        "APP",
        DEFAULT_APP_SOURCE_ROOT,
        DEFAULT_APP_OUTPUT_ROOT,
        mpy_cross,
        args.force,
    )
    if app_result != 0:
        return app_result

    if not args.no_lib:
        lib_result = build_tree(
            "LIB",
            DEFAULT_LIB_SOURCE_ROOT,
            DEFAULT_LIB_OUTPUT_ROOT,
            mpy_cross,
            args.force,
        )
        if lib_result != 0:
            return lib_result

    print("MPY build complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
