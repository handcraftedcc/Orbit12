import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.layout import compute_window_start


class Orbit12UILayoutTests(unittest.TestCase):
    def test_window_start_uses_lookahead_row(self):
        # With 4 visible rows and 1 preview row, scrolling starts at selected index 3.
        self.assertEqual(compute_window_start(0, item_count=10, visible_rows=4, preview_rows=1), 0)
        self.assertEqual(compute_window_start(1, item_count=10, visible_rows=4, preview_rows=1), 0)
        self.assertEqual(compute_window_start(2, item_count=10, visible_rows=4, preview_rows=1), 0)
        self.assertEqual(compute_window_start(3, item_count=10, visible_rows=4, preview_rows=1), 1)

    def test_window_start_clamps_at_end(self):
        self.assertEqual(compute_window_start(9, item_count=10, visible_rows=4, preview_rows=1), 6)

    def test_window_start_handles_short_lists(self):
        self.assertEqual(compute_window_start(1, item_count=3, visible_rows=4, preview_rows=1), 0)


if __name__ == "__main__":
    unittest.main()
