"""Tests for tools/modules/new_module.py. Scaffolds into temporary directories only."""
import importlib.util
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location("new_module", os.path.join(TOOLS, "new_module.py"))
new_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(new_module)
gen = new_module.gen


class NewModule(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="new-module-")
        self.saved = {name: getattr(gen, name) for name in ("USER_DIR", "EXAMPLE_DIR", "GEN_DIR", "TEST_GEN_DIR")}
        gen.USER_DIR = os.path.join(self.tmp, "user")
        gen.EXAMPLE_DIR = os.path.join(self.tmp, "examples")
        gen.GEN_DIR = os.path.join(self.tmp, "gen")
        gen.TEST_GEN_DIR = os.path.join(self.tmp, "test_gen")
        os.makedirs(gen.USER_DIR)
        os.makedirs(gen.EXAMPLE_DIR)

    def tearDown(self):
        for name, value in self.saved.items():
            setattr(gen, name, value)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_scaffold_creates_a_valid_module_and_regenerates(self):
        code = new_module.main(["new", "--kind", "processor", "--author", "alice", "--name", "octave_up"])
        self.assertEqual(code, 0)
        directory = os.path.join(gen.USER_DIR, "alice", "octave_up")
        for name in ("module.json", "octave_up.hpp", "octave_up.cpp"):
            self.assertTrue(os.path.isfile(os.path.join(directory, name)), name)
        with open(os.path.join(gen.GEN_DIR, "registry_generated.cpp"), encoding="utf-8") as handle:
            self.assertIn("user_alice_octave_up_module_descriptor()", handle.read())

    def test_input_scaffold_is_registered_in_the_union_and_registry(self):
        self.assertEqual(new_module.main(["new", "--kind", "input", "--author", "alice", "--name", "shift_keys"]), 0)
        directory = os.path.join(gen.USER_DIR, "alice", "shift_keys")
        self.assertTrue(os.path.isfile(os.path.join(directory, "shift_keys_state.hpp")))
        with open(os.path.join(gen.GEN_DIR, "input_state_generated.hpp"), encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("user_alice_shift_keys", text)
        self.assertIn("ShiftKeysInputState", text)
        with open(os.path.join(gen.GEN_DIR, "input_registry_generated.cpp"), encoding="utf-8") as handle:
            self.assertIn("user_alice_shift_keys_input_descriptor()", handle.read())

    def test_bad_author_and_name_are_refused(self):
        self.assertEqual(new_module.main(["new", "--kind", "processor", "--author", "Bad Name", "--name", "x"]), 1)
        self.assertEqual(new_module.main(["new", "--kind", "processor", "--author", "ok", "--name", "Bad-Name"]), 1)

    def test_rejected_module_is_rolled_back(self):
        # The scene key defaults to the name: "arp" clashes with the core arp module, so the generator refuses.
        self.assertEqual(new_module.main(["new", "--kind", "processor", "--author", "carol", "--name", "arp"]), 1)
        self.assertFalse(os.path.exists(os.path.join(gen.USER_DIR, "carol", "arp")))

    def test_existing_module_is_not_overwritten(self):
        self.assertEqual(new_module.main(["new", "--kind", "processor", "--author", "bob", "--name", "pad"]), 0)
        self.assertEqual(new_module.main(["new", "--kind", "processor", "--author", "bob", "--name", "pad"]), 1)


if __name__ == "__main__":
    unittest.main()
