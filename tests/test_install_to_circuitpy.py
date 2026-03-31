import importlib.util
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "install_to_circuitpy.py"


class InstallScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("install_to_circuitpy", SCRIPT_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        cls.mod = module

    def test_should_preserve_python_entrypoints(self):
        keep = self.mod.should_preserve_py
        self.assertTrue(keep("code.py"))
        self.assertTrue(keep("apps/Launcher/code.py"))
        self.assertTrue(keep("apps/ParamLab/code.py"))
        self.assertTrue(keep("apps/Orbit12MidiCommander/Orbit12MidiCommander.py"))

        self.assertFalse(keep("apps/Launcher/launcher_core.py"))
        self.assertFalse(keep("lib/orbit12ui/parameters.py"))

    def test_build_artifact_compiles_non_entry_py(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            src = td_path / "source"
            build = td_path / "build"
            src.mkdir()

            # source tree
            (src / "code.py").write_text("print('root')\n", encoding="utf-8")
            (src / "apps" / "Foo").mkdir(parents=True)
            (src / "apps" / "Foo" / "code.py").write_text("print('foo')\n", encoding="utf-8")
            (src / "apps" / "Foo" / "helper.py").write_text("X=1\n", encoding="utf-8")
            (src / "lib" / "pkg").mkdir(parents=True)
            (src / "lib" / "pkg" / "mod.py").write_text("Y=2\n", encoding="utf-8")
            (src / "lib" / "already.mpy").write_bytes(b"MPY")
            (src / "userdata").mkdir(parents=True)
            (src / "userdata" / "settings.json").write_text("{}\n", encoding="utf-8")

            # fake mpy-cross that writes output file
            fake_mpy = td_path / "fake_mpy_cross.py"
            fake_mpy.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "inp = sys.argv[-1]\n"
                "out = None\n"
                "for i, a in enumerate(sys.argv):\n"
                "    if a == '-o':\n"
                "        out = sys.argv[i+1]\n"
                "        break\n"
                "if out is None:\n"
                "    raise SystemExit(2)\n"
                "pathlib.Path(out).write_bytes(b'MPY:'+pathlib.Path(inp).read_bytes())\n",
                encoding="utf-8",
            )
            fake_mpy.chmod(0o755)

            self.mod.build_artifact(src, build, [str(fake_mpy)])

            self.assertTrue((build / "code.py").exists())
            self.assertTrue((build / "apps" / "Foo" / "code.py").exists())

            self.assertTrue((build / "apps" / "Foo" / "helper.mpy").exists())
            self.assertFalse((build / "apps" / "Foo" / "helper.py").exists())

            self.assertTrue((build / "lib" / "pkg" / "mod.mpy").exists())
            self.assertFalse((build / "lib" / "pkg" / "mod.py").exists())

            self.assertEqual((build / "lib" / "already.mpy").read_bytes(), b"MPY")
            self.assertTrue((build / "userdata" / "settings.json").exists())

    def test_build_rsync_command_excludes_userdata(self):
        cmd = self.mod.build_rsync_command("/tmp/build", "/Volumes/CIRCUITPY", dry_run=True)
        self.assertIn("--inplace", cmd)
        self.assertIn("--delete", cmd)
        self.assertIn("--exclude", cmd)
        self.assertIn("userdata/", cmd)
        self.assertIn("._*", cmd)
        self.assertIn("boot_out.txt", cmd)
        self.assertIn("--dry-run", cmd)

    def test_build_rsync_command_includes_dynamic_excludes(self):
        cmd = self.mod.build_rsync_command(
            "/tmp/build",
            "/Volumes/CIRCUITPY",
            dry_run=False,
            extra_excludes=["lib/adafruit_macropad.mpy", "lib/adafruit_midi/"],
        )
        self.assertIn("lib/adafruit_macropad.mpy", cmd)
        self.assertIn("lib/adafruit_midi/", cmd)

    def test_detect_existing_adafruit_paths(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            build = td_path / "build"
            device = td_path / "device"
            (build / "lib").mkdir(parents=True)
            (device / "lib").mkdir(parents=True)

            # build artifacts
            (build / "lib" / "adafruit_macropad.mpy").write_bytes(b"X")
            (build / "lib" / "adafruit_midi").mkdir()
            (build / "lib" / "adafruit_midi" / "__init__.mpy").write_bytes(b"X")
            (build / "lib" / "orbit12ui").mkdir()
            (build / "lib" / "orbit12ui" / "__init__.mpy").write_bytes(b"X")

            # device currently has Adafruit libs already
            (device / "lib" / "adafruit_macropad.mpy").write_bytes(b"OLD")
            (device / "lib" / "adafruit_midi").mkdir()
            (device / "lib" / "adafruit_midi" / "__init__.mpy").write_bytes(b"OLD")

            excludes = self.mod.detect_existing_adafruit_paths(build, device)
            self.assertIn("lib/adafruit_macropad.mpy", excludes)
            self.assertIn("lib/adafruit_midi/", excludes)
            self.assertNotIn("lib/orbit12ui/", excludes)

    def test_parse_args_reinstall_flag(self):
        args_default = self.mod.parse_args([])
        self.assertFalse(args_default.reinstall_adafruit_libraries)

        args_long = self.mod.parse_args(["--reinstall-adafruit-libraries"])
        self.assertTrue(args_long.reinstall_adafruit_libraries)

        args_compact = self.mod.parse_args(["--reinstalladafruitlibraries"])
        self.assertTrue(args_compact.reinstall_adafruit_libraries)

    def test_parse_args_no_compile_flag(self):
        args_default = self.mod.parse_args([])
        self.assertFalse(args_default.no_compile)

        args = self.mod.parse_args(["--no-compile"])
        self.assertTrue(args.no_compile)

    def test_is_circuitpython_mpy_file(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            cp = td_path / "cp.mpy"
            mp = td_path / "mp.mpy"
            cp.write_bytes(b"C\\x06\\x00\\x1f")
            mp.write_bytes(b"M\\x06\\x00\\x1f")

            self.assertTrue(self.mod.is_circuitpython_mpy_file(cp))
            self.assertFalse(self.mod.is_circuitpython_mpy_file(mp))

    def test_deploy_build_excludes_codepy_and_copies_last(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            build = td_path / "build"
            device = td_path / "device"
            build.mkdir()
            device.mkdir()
            (build / "code.py").write_text("print('ok')\n", encoding="utf-8")

            with mock.patch.object(self.mod.subprocess, "run") as run_mock:
                self.mod.deploy_build(build, device, dry_run=False, extra_excludes=["userdata/"])

            cmd = run_mock.call_args[0][0]
            self.assertIn("--exclude", cmd)
            self.assertIn("code.py", cmd)
            self.assertEqual((device / "code.py").read_text(encoding="utf-8"), "print('ok')\n")

    def test_deploy_build_retries_rsync_failures(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            build = td_path / "build"
            device = td_path / "device"
            build.mkdir()
            device.mkdir()
            (build / "code.py").write_text("print('ok')\n", encoding="utf-8")

            err = subprocess.CalledProcessError(1, ["rsync"])
            with mock.patch.object(self.mod.subprocess, "run", side_effect=[err, err, None]) as run_mock:
                self.mod.deploy_build(build, device, dry_run=False, extra_excludes=[])

            self.assertEqual(run_mock.call_count, 3)

if __name__ == "__main__":
    unittest.main()
