"""Root entrypoint: boot Orbit12 launcher."""

import os
import sys


def _file_exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False


def _launcher_path_candidates():
    return (
        "/apps/Launcher/code.py",
        "apps/Launcher/code.py",
        "DeviceFiles/apps/Launcher/code.py",
    )


def main():
    launcher_path = None
    for candidate in _launcher_path_candidates():
        if _file_exists(candidate):
            launcher_path = candidate
            break

    if launcher_path is None:
        raise RuntimeError("Launcher entry not found")

    launcher_dir = launcher_path.rsplit("/", 1)[0]
    if launcher_dir and launcher_dir not in sys.path:
        sys.path.insert(0, launcher_dir)

    namespace = {"__name__": "__main__", "__file__": launcher_path}
    with open(launcher_path, "r") as handle:
        source = handle.read()
    exec(compile(source, launcher_path, "exec"), namespace, namespace)


main()
