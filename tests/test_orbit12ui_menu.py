import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.menu import MenuController
from orbit12ui.parameters import create_parameter


class Orbit12UIMenuTests(unittest.TestCase):
    def test_navigation_and_edit_mode(self):
        items = [
            create_parameter({"type": "int", "name": "steps", "default": 0, "min": 0, "max": 8, "step": 1}),
            create_parameter({"type": "boolean", "name": "gate", "default": False, "mode": "onoff"}),
        ]
        menu = MenuController(items)

        self.assertEqual(menu.current_item().name, "..")

        menu.rotate(1)
        self.assertEqual(menu.current_item().name, "steps")

        menu.rotate(1)
        self.assertEqual(menu.current_item().name, "gate")

        menu.press()  # enter edit on gate
        self.assertTrue(menu.current_item().is_editing)

        menu.rotate(1)
        self.assertTrue(menu.current_item().value)

        menu.press()  # exit edit
        self.assertFalse(menu.current_item().is_editing)

    def test_folder_enter_and_exit(self):
        folder = create_parameter({
            "type": "folder",
            "name": "advanced",
            "children": [
                {"type": "int", "name": "inner", "min": 0, "max": 10, "default": 2},
            ],
        })
        menu = MenuController([folder])

        menu.rotate(1)
        menu.press()  # enter folder
        self.assertEqual(menu.current_item().name, "..")

        menu.rotate(1)
        self.assertEqual(menu.current_item().name, "inner")

        menu.rotate(-1)
        back_event = menu.press()
        self.assertEqual(back_event["type"], "back")
        self.assertEqual(menu.current_item().name, "..")

    def test_button_action_event(self):
        menu = MenuController([create_parameter({"type": "button", "name": "launch_app"})])
        menu.rotate(1)
        event = menu.press()
        self.assertEqual(event["type"], "button")
        self.assertEqual(event["name"], "launch_app")

    def test_press_back_on_root_returns_root_back_event(self):
        menu = MenuController([create_parameter({"type": "int", "name": "steps", "default": 1, "min": 0, "max": 8})])
        event = menu.press()
        self.assertEqual(event["type"], "root_back")

    def test_visibility_condition_hides_item(self):
        items = [
            create_parameter({"type": "int", "name": "mode", "min": 0, "max": 10, "default": 0}),
            create_parameter({"type": "int", "name": "detail", "min": 0, "max": 10, "default": 1, "viscondition": "mode > 3"}),
        ]
        menu = MenuController(items)

        names = [p.name for p in menu.visible_items()]
        self.assertEqual(names, ["..", "mode"])

        mode = items[0]
        mode.start_edit()
        mode.rotate(4)
        mode.stop_edit()

        names = [p.name for p in menu.visible_items()]
        self.assertEqual(names, ["..", "mode", "detail"])


if __name__ == "__main__":
    unittest.main()
