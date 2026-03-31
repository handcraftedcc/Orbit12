import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.textfit import fit_single_line, fit_split_line


class Orbit12UITextFitTests(unittest.TestCase):
    def test_single_line_is_always_fixed_width(self):
        line = fit_single_line("This is a very very long app name", width=20, selected=False, tick=0)
        self.assertEqual(len(line), 20)
        self.assertTrue(line.endswith(".."))

    def test_single_line_scrolls_when_selected(self):
        first = fit_single_line("This is a very very long app name", width=20, selected=True, tick=0)
        second = fit_single_line("This is a very very long app name", width=20, selected=True, tick=3)
        self.assertEqual(len(first), 20)
        self.assertEqual(len(second), 20)
        self.assertNotEqual(first, second)

    def test_split_line_is_always_fixed_width(self):
        line = fit_split_line(
            "A label name that is too long",
            "A very long value that also overflows",
            width=20,
            selected=False,
            tick=0,
        )
        self.assertEqual(len(line), 20)
        self.assertIn(" ", line)

    def test_split_line_scrolls_on_selected_overflow(self):
        first = fit_split_line(
            "A label name that is too long",
            "A very long value that also overflows",
            width=20,
            selected=True,
            tick=0,
        )
        second = fit_split_line(
            "A label name that is too long",
            "A very long value that also overflows",
            width=20,
            selected=True,
            tick=4,
        )
        self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()
