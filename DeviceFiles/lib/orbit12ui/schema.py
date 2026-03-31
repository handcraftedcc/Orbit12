"""Schema helpers for loading Orbit12 UI JSON files."""

import json

from orbit12ui.parameters import create_parameter


def build_tabs(schema):
    tabs = []
    for raw_tab in schema.get("tabs", []):
        tab_name = raw_tab.get("name", "tab")
        tab_label = raw_tab.get("label") or tab_name
        tab_items = [create_parameter(item) for item in raw_tab.get("items", [])]
        tabs.append({"name": tab_name, "label": tab_label, "items": tab_items})
    return tabs


def load_ui_json(path):
    with open(path, "r") as handle:
        schema = json.load(handle)
    return build_tabs(schema)
