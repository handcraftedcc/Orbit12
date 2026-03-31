"""Core app discovery and launch helpers for Orbit12 Launcher."""

import json
import os
import sys


def _is_dir(path):
    try:
        return os.path.isdir(path)
    except AttributeError:
        try:
            mode = os.stat(path)[0]
        except OSError:
            return False
        return bool(mode & 0x4000)


def _is_file(path):
    if _is_dir(path):
        return False
    try:
        os.stat(path)
        return True
    except OSError:
        return False


def discover_apps(apps_root="/apps"):
    """Discover launchable apps under apps_root.

    Each app must have either `code.py` or `<AppName>.py`.
    Launcher app is excluded from results.
    """
    results = []

    try:
        entries = sorted(os.listdir(apps_root))
    except OSError:
        return results

    for name in entries:
        if name == "Launcher":
            continue

        app_dir = apps_root.rstrip("/") + "/" + name
        if not _is_dir(app_dir):
            continue

        candidate_code = app_dir + "/code.py"
        candidate_named = app_dir + "/" + name + ".py"

        entry_file = None
        if _is_file(candidate_code):
            entry_file = candidate_code
        elif _is_file(candidate_named):
            entry_file = candidate_named

        if entry_file:
            results.append({"name": name, "entry": entry_file})

    return sorted(results, key=lambda item: item["name"].lower())


def build_menu_rows(apps):
    rows = [app["name"] + " >" for app in apps]
    rows.append("[Settings]")
    return rows


def load_global_settings(path="/userdata/global_settings.json"):
    settings = {"device_rotation": "default"}
    try:
        with open(path, "r") as handle:
            raw = handle.read().strip()
            if not raw:
                return settings
            loaded = json.loads(raw)
            if isinstance(loaded, dict):
                settings.update(loaded)
    except OSError:
        # Fallback for host-like relative root behavior.
        if path.startswith("/"):
            try:
                with open(path[1:], "r") as handle:
                    raw = handle.read().strip()
                    if raw:
                        loaded = json.loads(raw)
                        if isinstance(loaded, dict):
                            settings.update(loaded)
            except (OSError, ValueError):
                pass
        return settings
    except ValueError:
        return settings
    return settings


def save_global_settings(settings, path="/userdata/global_settings.json"):
    _ensure_parent_dirs(path)
    payload = {"device_rotation": settings.get("device_rotation", "default")}
    with open(path, "w") as handle:
        json.dump(payload, handle)


def rotation_from_settings(settings):
    if settings.get("device_rotation") == "flip":
        return 180
    return 0


def _mkdir(path):
    try:
        os.mkdir(path)
    except OSError:
        pass


def _ensure_parent_dirs(file_path):
    norm = file_path.replace("\\", "/")
    if "/" not in norm:
        return

    parent = norm.rsplit("/", 1)[0]
    if not parent:
        return

    parts = [part for part in parent.split("/") if part]
    current = "/" if parent.startswith("/") else ""

    for part in parts:
        if current in ("", "/"):
            next_path = current + part
        else:
            next_path = current + "/" + part
        _mkdir(next_path)
        current = next_path


def execute_entry(entry_path, shared_globals=None):
    """Execute an app entry file."""
    app_dir = entry_path.rsplit("/", 1)[0]
    if app_dir and app_dir not in sys.path:
        sys.path.insert(0, app_dir)

    namespace = {"__name__": "__main__", "__file__": entry_path}
    if shared_globals:
        namespace.update(shared_globals)
    with open(entry_path, "r") as handle:
        source = handle.read()
    exec(compile(source, entry_path, "exec"), namespace, namespace)
