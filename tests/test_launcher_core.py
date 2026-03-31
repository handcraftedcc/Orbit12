import pathlib
import shutil
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER_PATH = ROOT / "DeviceFiles" / "apps" / "Launcher"
if str(LAUNCHER_PATH) not in sys.path:
    sys.path.insert(0, str(LAUNCHER_PATH))

from launcher_core import build_menu_rows, discover_apps
from launcher_core import execute_entry
from launcher_core import load_global_settings, rotation_from_settings, save_global_settings


class LauncherCoreTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = pathlib.Path(tempfile.mkdtemp(prefix="orbit12_apps_"))

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def _create_app(self, name, file_name):
        app_dir = self.temp_dir / name
        app_dir.mkdir(parents=True, exist_ok=True)
        (app_dir / file_name).write_text("# test\n", encoding="utf-8")

    def test_discover_apps_excludes_launcher_and_requires_entry_file(self):
        self._create_app("Launcher", "code.py")
        self._create_app("GoodCode", "code.py")
        self._create_app("GoodNamed", "GoodNamed.py")

        (self.temp_dir / "BrokenNoEntry").mkdir(parents=True, exist_ok=True)

        apps = discover_apps(str(self.temp_dir))
        names = [app["name"] for app in apps]

        self.assertEqual(names, ["GoodCode", "GoodNamed"])

    def test_build_menu_rows_formats_and_appends_settings(self):
        apps = [{"name": "AppA", "entry": "/tmp/a.py"}, {"name": "AppB", "entry": "/tmp/b.py"}]
        rows = build_menu_rows(apps)
        self.assertEqual(rows, ["AppA >", "AppB >", "[Settings]"])

    def test_execute_entry_supports_local_app_imports(self):
        app_dir = self.temp_dir / "LocalImportApp"
        app_dir.mkdir(parents=True, exist_ok=True)

        (app_dir / "helper.py").write_text("VALUE = 42\n", encoding="utf-8")
        marker_file = app_dir / "marker.txt"
        (app_dir / "code.py").write_text(
            "import helper\n"
            "with open(r'" + str(marker_file) + "', 'w', encoding='utf-8') as handle:\n"
            "    handle.write(str(helper.VALUE))\n",
            encoding="utf-8",
        )

        execute_entry(str(app_dir / "code.py"))
        self.assertEqual(marker_file.read_text(encoding="utf-8"), "42")

    def test_execute_entry_passes_shared_globals(self):
        app_dir = self.temp_dir / "SharedGlobalApp"
        app_dir.mkdir(parents=True, exist_ok=True)
        marker_file = app_dir / "marker.txt"
        (app_dir / "code.py").write_text(
            "flag = 'missing'\n"
            "if 'SHARED_MACROPAD' in globals():\n"
            "    flag = 'ok'\n"
            "with open(r'" + str(marker_file) + "', 'w', encoding='utf-8') as handle:\n"
            "    handle.write(flag)\n",
            encoding="utf-8",
        )

        execute_entry(str(app_dir / "code.py"), shared_globals={"SHARED_MACROPAD": object()})
        self.assertEqual(marker_file.read_text(encoding="utf-8"), "ok")

    def test_global_settings_roundtrip_and_rotation_mapping(self):
        settings_path = self.temp_dir / "global_settings.json"

        defaults = load_global_settings(str(settings_path))
        self.assertEqual(defaults.get("device_rotation"), "default")
        self.assertEqual(rotation_from_settings(defaults), 0)

        save_global_settings({"device_rotation": "flip"}, str(settings_path))
        loaded = load_global_settings(str(settings_path))
        self.assertEqual(loaded.get("device_rotation"), "flip")
        self.assertEqual(rotation_from_settings(loaded), 180)

    def test_save_global_settings_creates_parent_dirs(self):
        settings_path = self.temp_dir / "userdata" / "global_settings.json"
        save_global_settings({"device_rotation": "default"}, str(settings_path))
        self.assertTrue(settings_path.exists())


if __name__ == "__main__":
    unittest.main()
