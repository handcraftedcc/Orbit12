import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.schema import build_tabs, load_ui_json


class Orbit12UISchemaTests(unittest.TestCase):
    def test_build_tabs_with_label_fallback_and_nested_folder(self):
        schema = {
            "tabs": [
                {
                    "name": "main",
                    "items": [
                        {"type": "int", "name": "steps", "default": 4, "min": 0, "max": 16},
                        {
                            "type": "folder",
                            "name": "advanced",
                            "children": [
                                {"type": "boolean", "name": "humanize", "mode": "onoff", "default": False}
                            ],
                        },
                    ],
                }
            ]
        }

        tabs = build_tabs(schema)
        self.assertEqual(len(tabs), 1)
        self.assertEqual(tabs[0]["label"], "main")

        steps = tabs[0]["items"][0]
        self.assertEqual(steps.label, "steps")

        folder = tabs[0]["items"][1]
        self.assertEqual(folder.children[0].name, "humanize")

    def test_load_ui_json(self):
        schema = {
            "tabs": [
                {"name": "a", "label": "A", "items": [{"type": "enum", "name": "mode", "items": ["X", "Y"]}]}
            ]
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            json.dump(schema, handle)
            path = handle.name

        tabs = load_ui_json(path)
        self.assertEqual(tabs[0]["name"], "a")
        self.assertEqual(tabs[0]["items"][0].value, "X")


if __name__ == "__main__":
    unittest.main()
