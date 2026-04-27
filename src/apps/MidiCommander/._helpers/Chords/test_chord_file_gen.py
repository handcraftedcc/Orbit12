import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("ChordFileGen.py")


def load_chord_file_gen():
    spec = importlib.util.spec_from_file_location("ChordFileGen", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ChordFileGenTest(unittest.TestCase):
    def test_output_file_is_under_build_directory(self):
        chord_file_gen = load_chord_file_gen()

        self.assertEqual(
            chord_file_gen.OUT_FILE,
            MODULE_PATH.with_name("Build") / "chords.py",
        )

    def test_parse_major_sixth_suffix_spelled_m6(self):
        chord_file_gen = load_chord_file_gen()

        self.assertEqual(
            chord_file_gen.parse_symbol("IIIM6"),
            (
                2,
                0,
                (0, 4, 7, 9),
            ),
        )
