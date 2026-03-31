"""Schema helpers for loading Orbit12 UI JSON files."""

import json

from orbit12ui.parameters import create_parameter


def _normalize_schema(schema):
    if isinstance(schema, dict):
        if "tabs" in schema:
            return schema
        if "items" in schema:
            return {"tabs": [schema]}
        raise ValueError("UI schema dict must include 'tabs' or 'items'")

    if isinstance(schema, list):
        return {"tabs": schema}

    raise ValueError("UI schema must be a dict or list")


def build_tabs(schema):
    schema = _normalize_schema(schema)
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


def load_ui_python(path):
    namespace = {
        "__name__": "__orbit12_ui__",
        "__file__": path,
    }
    with open(path, "r") as handle:
        source = handle.read()
    exec(source, namespace)

    if callable(namespace.get("build_ui")):
        schema = namespace["build_ui"]()
    elif "UI_SCHEMA" in namespace:
        schema = namespace["UI_SCHEMA"]
    elif "TABS" in namespace:
        schema = namespace["TABS"]
    else:
        raise ValueError("Python UI source must define build_ui(), UI_SCHEMA, or TABS")

    return build_tabs(schema)


def load_ui(path):
    if path.lower().endswith(".json"):
        return load_ui_json(path)
    if path.lower().endswith(".py"):
        return load_ui_python(path)
    raise ValueError("Unsupported UI source: " + path)
