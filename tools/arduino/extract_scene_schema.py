#!/usr/bin/env python3
"""Extract the source-declared SCN1 fields without importing CircuitPython modules.

The script is intentionally static: module constructors depend on board services,
but persisted field names, Parm types, save_attrs declarations, and fixed array
capacities are present in the checked-in Python source.
"""
from __future__ import annotations

import ast
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
ORION = ROOT / "circuitpython-source" / "src" / "apps" / "Orion"
UI_ONLY = {"active_scene", "save_scene", "copy_scene", "reset_scene", "key_brightness"}


def source(node: ast.AST | None) -> str | None:
    return ast.unparse(node) if node is not None else None


def literal_string(node: ast.AST | None) -> str | None:
    return node.value if isinstance(node, ast.Constant) and isinstance(node.value, str) else None


def class_constant(tree: ast.Module, name: str) -> ast.AST | None:
    for cls in (node for node in tree.body if isinstance(node, ast.ClassDef)):
        for stmt in cls.body:
            targets = stmt.targets if isinstance(stmt, ast.Assign) else [stmt.target] if isinstance(stmt, ast.AnnAssign) else []
            for target in targets:
                if isinstance(target, ast.Name) and target.id == name:
                    return stmt.value
    return None


def sequence_value(node: ast.AST | None) -> list[object]:
    if isinstance(node, (ast.Tuple, ast.List)):
        return [elt.value if isinstance(elt, ast.Constant) else source(elt) for elt in node.elts]
    return []


def module_id(tree: ast.Module, fallback: str) -> str:
    for cls in (node for node in tree.body if isinstance(node, ast.ClassDef)):
        value = literal_string(class_constant(ast.Module(body=[cls], type_ignores=[]), "name"))
        if value:
            return value
    return fallback


def array_bound(text: str, path: pathlib.Path) -> int | None:
    if re.search(r"PAD_COUNT", text):
        match = re.search(r"^PAD_COUNT\s*=\s*(\d+)", text, re.M)
        if match:
            return int(match.group(1))
    if re.search(r"LANES", text):
        match = re.search(r"^LANES\s*=\s*(\d+)", text, re.M)
        if match:
            return int(match.group(1))
    if re.search(r"MAX_STEPS", text):
        match = re.search(r"^MAX_STEPS\s*=\s*(\d+)", text, re.M)
        if match:
            return int(match.group(1))
    return None


def loop_multiplicity(node: ast.AST, parents: dict[ast.AST, ast.AST]) -> int:
    """Multiply literal range-loop sizes surrounding a constructor call."""
    count = 1
    current = node
    while current in parents:
        current = parents[current]
        if isinstance(current, (ast.For, ast.AsyncFor)) and isinstance(current.iter, ast.Call):
            if isinstance(current.iter.func, ast.Name) and current.iter.func.id == "range":
                args = current.iter.args
                try:
                    values = [ast.literal_eval(arg) for arg in args]
                    if len(values) == 1:
                        size = max(0, int(values[0]))
                    elif len(values) in (2, 3):
                        start, stop = int(values[0]), int(values[1])
                        step = int(values[2]) if len(values) == 3 else 1
                        size = len(range(start, stop, step))
                    else:
                        size = 1
                    count *= size
                except (ValueError, TypeError):
                    pass
        elif isinstance(current, (ast.For, ast.AsyncFor)) and isinstance(current.iter, ast.Name):
            # Source parameter tables such as matrix.PAD_VALUE_PARMS are literal tuples.
            for ancestor in parents:
                if isinstance(ancestor, (ast.Assign, ast.AnnAssign)):
                    targets = ancestor.targets if isinstance(ancestor, ast.Assign) else [ancestor.target]
                    if any(isinstance(target, ast.Name) and target.id == current.iter.id for target in targets):
                        if isinstance(ancestor.value, (ast.Tuple, ast.List)):
                            count *= len(ancestor.value.elts)
                        break
    return count


def collect(path: pathlib.Path, category: str) -> dict[str, object]:
    text = path.read_text(encoding="utf-8")
    tree = ast.parse(text, filename=str(path))
    parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}
    module = module_id(tree, path.stem)
    saves = sequence_value(class_constant(tree, "save_attrs"))
    bound = array_bound(text, path)
    parameters: list[dict[str, object]] = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "Parm"):
            continue
        kwargs = {kw.arg: kw.value for kw in node.keywords if kw.arg}
        positional = node.args
        name_node = kwargs.get("name", positional[0] if positional else None)
        type_node = kwargs.get("parm_type", positional[2] if len(positional) > 2 else None)
        default_node = kwargs.get("default", positional[3] if len(positional) > 3 else None)
        minmax_node = kwargs.get("minmax")
        type_expr = source(type_node) or "Parms.IntParmType (Parm default)"
        name = literal_string(name_node)
        names: list[str] = [name] if name is not None else []
        if name is None and "offset" in (source(name_node) or "") and path.stem == "chord":
            names = [f"offset{i}" for i in range(1, 6)]
        if name is None and category == "input" and saves and "getattr" in (source(default_node) or ""):
            # Matrix/chord_walk expose one selected saved attribute as a parameter.
            names = [str(value) for value in saves if isinstance(value, str)]
        is_button = "ButtonParmType" in type_expr
        multiplicity = loop_multiplicity(node, parents)
        if names and len(names) > 1 and multiplicity == 1 and not (
            path.stem == "chord" and "offset" in (source(name_node) or "")
        ):
            multiplicity = 1
        parameters.append({
            "names": names,
            "name_expression": source(name_node),
            "type": type_expr,
            "default_expression": source(default_node),
            "minmax_expression": source(minmax_node),
            "persisted": not is_button and not any(value in UI_ONLY for value in names),
            "constructor_multiplicity": multiplicity,
            "line": node.lineno,
        })
    local_parameter_count = sum(
        int(item["constructor_multiplicity"]) for item in parameters if item["persisted"]
    )
    inherited_parameter_count = 0
    if category == "processor":
        # Module._build_parms appends source_mode plus operation_mode/out_channel.
        inherited_parameter_count = 3
    elif category == "input":
        # Input.create_main_parms supplies octave/key_offset/velocity when enabled;
        # Input.create_out_parms replaces Module.create_out_parms but calls super,
        # retaining operation_mode and out_channel (2 additional records).
        constructor_text = text
        musical = "include_musical_parms=False" not in constructor_text
        inherited_parameter_count = (3 if musical else 0) + 2
    return {
        "category": category,
        "id": module,
        "source": path.relative_to(ORION).as_posix(),
        "parameters": parameters,
        "local_persisted_parameter_count": local_parameter_count,
        "inherited_persisted_parameter_count": inherited_parameter_count,
        "persisted_parameter_count": local_parameter_count + inherited_parameter_count,
        "saved_attributes": [
            {"name": name, "element_count": bound, "source_declared": True}
            for name in saves if isinstance(name, str)
        ],
    }


def main() -> int:
    entries: list[dict[str, object]] = []
    for category, folder in (("processor", "modules"), ("input", "inputmodules")):
        for path in sorted((ORION / folder).glob("*.py")):
            if path.name.startswith("_"):
                continue
            entries.append(collect(path, category))
    for path in sorted((ORION / "core" / "_modules").glob("_*.py")):
        if path.name in {"__init__.py", "_empty.py"}:
            category = "fixed"
        else:
            category = "fixed"
        entries.append(collect(path, category))
    entries.sort(key=lambda item: (str(item["category"]), str(item["id"])))
    result = {
        "format": "SCN1",
        "source": "circuitpython-source/src/apps/Orion/core/scenes.py",
        "scene_ui_parameter_exclusions": sorted(UI_ONLY),
        "string_parameter_count": sum(
            1 for entry in entries for parm in entry["parameters"]
            if "StringParmType" in str(parm["type"])
        ),
        "modules": entries,
    }
    by_id = {str(entry["id"]): entry for entry in entries}
    expected = {"matrix": 17, "chords": 13, "arp_walk": 15}
    for module_id, count in expected.items():
        if module_id not in by_id or by_id[module_id]["persisted_parameter_count"] != count:
            raise ValueError(
                f"source parameter inventory changed: expected {module_id}={count}; "
                f"review scene_schema.hpp bounds and update this assertion"
            )
    max_attribute_name = max(
        len(str(attribute["name"]))
        for entry in entries for attribute in entry["saved_attributes"]
    )
    if max_attribute_name != 11:
        raise ValueError(
            f"source attribute name bound changed: expected 11, found {max_attribute_name}; "
            "review scene_schema.hpp bounds and update this assertion"
        )
    json.dump(result, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
