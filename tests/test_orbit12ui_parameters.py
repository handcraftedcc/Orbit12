import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.conditions import evaluate_condition
from orbit12ui.parameters import create_parameter


class Orbit12UIParameterTests(unittest.TestCase):
    def test_int_parameter_clamps_and_steps(self):
        p = create_parameter({"type": "int", "name": "steps", "min": 0, "max": 16, "step": 2, "default": 4})
        p.start_edit()
        p.rotate(2)
        self.assertEqual(p.value, 8)
        p.rotate(10)
        self.assertEqual(p.value, 16)
        p.rotate(-20)
        self.assertEqual(p.value, 0)

    def test_float_percent_display(self):
        p = create_parameter({
            "type": "float",
            "name": "mix",
            "min": -2.0,
            "max": 2.0,
            "step": 0.25,
            "default": 0.5,
            "decimals": 1,
            "mode": "percent",
        })
        self.assertEqual(p.display_value(), "50.0%")
        p.start_edit()
        p.rotate(3)
        self.assertEqual(p.value, 1.25)
        self.assertEqual(p.display_value(), "125.0%")

    def test_boolean_modes(self):
        tf = create_parameter({"type": "boolean", "name": "sync", "mode": "truefalse", "default": True})
        onoff = create_parameter({"type": "boolean", "name": "active", "mode": "onoff", "default": False})

        self.assertEqual(tf.display_value(), "True")
        self.assertEqual(onoff.display_value(), "Off")

        tf.start_edit()
        tf.rotate(1)
        self.assertFalse(tf.value)
        self.assertEqual(tf.display_value(), "False")

        onoff.start_edit()
        onoff.rotate(1)
        self.assertTrue(onoff.value)
        self.assertEqual(onoff.display_value(), "On")

    def test_enum_wrap_modes(self):
        wrap = create_parameter({"type": "enum", "name": "mode", "items": ["A", "B", "C"], "wrap": True, "default": 0})
        nowrap = create_parameter({"type": "enum", "name": "mode2", "items": ["A", "B", "C"], "wrap": False, "default": 0})

        wrap.start_edit()
        wrap.rotate(-1)
        self.assertEqual(wrap.value, "C")

        nowrap.start_edit()
        nowrap.rotate(-1)
        self.assertEqual(nowrap.value, "A")

    def test_string_editor_flow(self):
        p = create_parameter({"type": "string", "name": "title", "default": ""})
        p.start_edit()

        self.assertEqual(p.display_value(), "[A]")

        p.rotate(1)  # B
        p.press()    # append B
        self.assertEqual(p.value, "B")
        self.assertEqual(p.display_value(), "B[A]")

        p.rotate(-1)  # to ✗
        p.rotate(-1)  # to ←
        p.press()     # delete B
        self.assertEqual(p.value, "")

        while p.current_symbol() != "C":
            p.rotate(1)
        p.press()    # append C
        self.assertEqual(p.value, "C")

        while p.current_symbol() != "✓":
            p.rotate(-1)
        done = p.press()
        self.assertTrue(done)
        self.assertFalse(p.is_editing)

    def test_rate_generation_options(self):
        full = create_parameter({"type": "rate", "name": "rate", "includeBars": True, "includeTriplets": True})
        no_bars = create_parameter({"type": "rate", "name": "rate2", "includeBars": False, "includeTriplets": True})
        no_triplets = create_parameter({"type": "rate", "name": "rate3", "includeBars": True, "includeTriplets": False})

        self.assertIn("16 bars", full.items)
        self.assertIn("1/1T", full.items)
        self.assertNotIn("16 bars", no_bars.items)
        self.assertNotIn("1/1T", no_triplets.items)

    def test_note_generation_options(self):
        pitch_only = create_parameter({"type": "note", "name": "note", "includeOctaves": False})
        with_oct = create_parameter({"type": "note", "name": "note2", "includeOctaves": True, "octaveRange": [-1, 1]})

        self.assertEqual(pitch_only.items[0], "C")
        self.assertNotIn("C0", pitch_only.items)
        self.assertIn("C-1", with_oct.items)
        self.assertIn("B1", with_oct.items)

    def test_button_and_folder_display(self):
        button = create_parameter({"type": "button", "name": "launch"})
        folder = create_parameter({"type": "folder", "name": "advanced", "children": []})
        self.assertEqual(button.display_value(), "Button >")
        self.assertEqual(folder.display_value(), "")

    def test_viscondition_operators(self):
        context = {"a": 3, "b": 5, "s": "ARP", "x": "OTHER"}
        self.assertTrue(evaluate_condition("a < b", context))
        self.assertTrue(evaluate_condition("a <= 3", context))
        self.assertTrue(evaluate_condition("b >= 5", context))
        self.assertTrue(evaluate_condition("a != 4", context))
        self.assertTrue(evaluate_condition("s == \"ARP\"", context))
        self.assertTrue(evaluate_condition("x != \"ARP\"", context))
        self.assertFalse(evaluate_condition("a > b", context))


if __name__ == "__main__":
    unittest.main()
