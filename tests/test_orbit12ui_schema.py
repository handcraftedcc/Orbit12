import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_PATH = ROOT / "DeviceFiles" / "lib"
if str(LIB_PATH) not in sys.path:
    sys.path.insert(0, str(LIB_PATH))

from orbit12ui.schema import build_tabs, load_ui, load_ui_json, load_ui_python


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

    def test_load_ui_python_with_ui_schema_constant(self):
        source = """
UI_SCHEMA = {
    "tabs": [
        {"name": "dyn", "items": [{"type": "int", "name": "steps", "min": 0, "max": 8, "default": 3}]}
    ]
}
"""
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as handle:
            handle.write(source)
            path = handle.name

        tabs = load_ui_python(path)
        self.assertEqual(tabs[0]["name"], "dyn")
        self.assertEqual(tabs[0]["items"][0].value, 3)

    def test_load_ui_python_with_build_function(self):
        source = """
def build_ui():
    values = ["A", "B", "C"]
    return {
        "tabs": [
            {"name": "generated", "items": [{"type": "enum", "name": "mode", "items": values}]}
        ]
    }
"""
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as handle:
            handle.write(source)
            path = handle.name

        tabs = load_ui_python(path)
        self.assertEqual(tabs[0]["name"], "generated")
        self.assertEqual(tabs[0]["items"][0].value, "A")

    def test_load_ui_auto_selects_loader_by_extension(self):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as json_handle:
            json.dump({"tabs": [{"name": "j", "items": [{"type": "boolean", "name": "gate"}]}]}, json_handle)
            json_path = json_handle.name
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as py_handle:
            py_handle.write("TABS = [{'name': 'p', 'items': [{'type': 'int', 'name': 'v', 'min': 0, 'max': 1}]}]")
            py_path = py_handle.name

        json_tabs = load_ui(json_path)
        py_tabs = load_ui(py_path)

        self.assertEqual(json_tabs[0]["name"], "j")
        self.assertEqual(py_tabs[0]["name"], "p")


if __name__ == "__main__":
    unittest.main()
