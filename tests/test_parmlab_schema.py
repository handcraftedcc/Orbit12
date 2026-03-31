import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PARAMLAB_JSON = ROOT / "DeviceFiles" / "apps" / "ParamLab" / "ui.json"


class ParamLabSchemaTests(unittest.TestCase):
    def test_parmlab_schema_covers_required_types_and_options(self):
        self.assertTrue(PARAMLAB_JSON.exists(), "ParamLab ui.json is missing")

        with PARAMLAB_JSON.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)

        self.assertIn("tabs", payload)

        items = []

        def collect(entry_list):
            for item in entry_list:
                items.append(item)
                if item.get("type") == "folder":
                    collect(item.get("children", []))

        for tab in payload["tabs"]:
            collect(tab.get("items", []))

        types = [item.get("type") for item in items]

        # Base type coverage
        for required in ["int", "float", "boolean", "enum", "string", "rate", "note", "button", "folder"]:
            self.assertIn(required, types, "Missing type: " + required)

        # Option variants requested in spec
        float_modes = {item.get("mode") for item in items if item.get("type") == "float"}
        self.assertIn("default", float_modes)
        self.assertIn("percent", float_modes)

        bool_modes = {item.get("mode") for item in items if item.get("type") == "boolean"}
        self.assertIn("truefalse", bool_modes)
        self.assertIn("onoff", bool_modes)

        enum_wrap_values = {item.get("wrap", True) for item in items if item.get("type") == "enum"}
        self.assertIn(True, enum_wrap_values)
        self.assertIn(False, enum_wrap_values)

        rate_shapes = [item for item in items if item.get("type") == "rate"]
        self.assertTrue(any(x.get("includeBars", True) and x.get("includeTriplets", True) for x in rate_shapes))
        self.assertTrue(any((not x.get("includeBars", True)) for x in rate_shapes))
        self.assertTrue(any((not x.get("includeTriplets", True)) for x in rate_shapes))

        note_shapes = [item for item in items if item.get("type") == "note"]
        self.assertTrue(any(not x.get("includeOctaves", True) for x in note_shapes))
        self.assertTrue(any(x.get("includeOctaves", False) and x.get("octaveRange") == [-2, 2] for x in note_shapes))

        self.assertTrue(any("viscondition" in item for item in items), "Expected at least one viscondition example")


if __name__ == "__main__":
    unittest.main()
