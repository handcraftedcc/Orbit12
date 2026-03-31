import importlib.util
import pathlib
import tempfile
import unittest

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
        self.assertIn("--delete", cmd)
        self.assertIn("--exclude", cmd)
        self.assertIn("userdata/", cmd)
        self.assertIn("--dry-run", cmd)


if __name__ == "__main__":
    unittest.main()
