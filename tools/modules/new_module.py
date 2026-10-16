#!/usr/bin/env python3
"""Scaffold a user module (processor or input) and regenerate the module tables.

  python -I tools/modules/new_module.py new --kind processor --author alice --name octave_up
  python -I tools/modules/new_module.py new --kind input --author alice --name shift_keys

Creates firmware/Orbit12/src/modules/user/<author>/<name>/ with module.json and compiling sources, then runs
gen_registry.py. The scaffold is compiled into the firmware (it is part of the build until you change or
remove it). Edit module.json (label, descriptor, parameters) and the sources, then run gen_registry.py again.

Processor: a stateless pass-through (state type EmptyModuleState, no state header).
Input: one saved integer parameter. Its state struct lives in <name>_state.hpp, which the generated state union
includes (see docs/modules/README.md).
If the generator rejects the module, the new directory is removed again.
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("gen_registry", os.path.join(HERE, "gen_registry.py"))
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)

AUTHOR_RE = re.compile(r"^[a-z0-9][a-z0-9_]{0,15}$")
NAME_RE = re.compile(r"^[a-z][a-z0-9_]{0,31}$")

PROCESSOR_HEADER = """#pragma once
// User processor {module_id}. Scaffolded by tools/modules/new_module.py.
#include "../../../common/module_state.hpp"

namespace orbit {{
const ModuleDescriptor& {accessor}() noexcept;
}}  // namespace orbit
"""

PROCESSOR_SOURCE = """// User processor {module_id}: passes notes through unchanged until you edit it.
// Rules: docs/modules/README.md (no heap, no logging, no blocking, capacity-checked appends).
#include "{name}.hpp"

namespace orbit {{
namespace {{
void construct(ModuleState&) noexcept {{}}
void reset(ModuleState&, ResetReason) noexcept {{}}
void destroy(ModuleState&) noexcept {{}}
void attributes(ModuleState&, AttributeVisitor&) noexcept {{}}
void process(ModuleState&, const ProcessContext&, const NoteBatch& input, NoteBatch& output) noexcept {{
  output = input;  // pass-through; a full output sets the sticky overflow flag
}}
const ModuleDescriptor kDescriptor{{"{module_id}", 1, nullptr, 0, construct, destroy, process, reset,
    nullptr, attributes}};
}}  // namespace

const ModuleDescriptor& {accessor}() noexcept {{ return kDescriptor; }}
}}  // namespace orbit
"""

INPUT_HEADER = """#pragma once
// User input {module_id}. Scaffolded by tools/modules/new_module.py.
#include "../../../common/input_state.hpp"

namespace orbit {{
const InputDescriptor& {accessor}() noexcept;
}}  // namespace orbit
"""

INPUT_STATE = """#pragma once
// State of the user input {module_id}. Included by the generated input state union
// (modules/generated/input_state_generated.hpp). Keep it to plain fields: no includes beyond this file's needs.
#include <cstdint>

namespace orbit {{
struct {type_name} {{
  std::int32_t value = 0;  // saved parameter "value"
}};
}}  // namespace orbit
"""

INPUT_SOURCE = """// User input {module_id}: one saved parameter, no notes yet. Until you add key handling, the input
// sends nothing and draws nothing; the parameter round-trips through scenes.
#include <new>

#include "{name}.hpp"
#include "{name}_state.hpp"

namespace orbit {{
namespace {{
const ParamSpec kParams[] = {{
    {{"value", ParamType::Integer, ParamValue::integer(0), ParamValue::integer(-12), ParamValue::integer(12),
     ParamValue::integer(1), "VALUE", nullptr, 0, ParamPersist, GlobalBinding::None}},
}};
void construct(InputState& state) noexcept {{ new (&state.storage.{member}) {type_name}{{}}; }}
void destroy(InputState&) noexcept {{}}
void reset(InputState&, ResetReason) noexcept {{}}
void process(InputState&, const ProcessContext&, const InputFrame&, NoteBatch& output, InputView&) noexcept {{
  output = {{}};
}}
bool set_param(InputState& state, std::uint8_t index, ParamValue value, const GlobalState&,
               std::uint8_t&) noexcept {{
  if (index != 0 || value.type != ParamType::Integer) return false;
  state.storage.{member}.value = value.value.integer;
  return true;
}}
void attributes(InputState&, AttributeVisitor&) noexcept {{}}
const InputDescriptor kDescriptor{{"{module_id}", 1, kParams, 1, construct, destroy, process, reset, set_param,
    attributes}};
}}  // namespace

const InputDescriptor& {accessor}() noexcept {{ return kDescriptor; }}
}}  // namespace orbit
"""


def camel(name):
    return "".join(part.capitalize() for part in name.split("_"))


def scaffold(kind, author, name, user_root):
    if kind not in ("processor", "input"):
        raise gen.GenError("kind must be processor or input")
    if not AUTHOR_RE.match(author or ""):
        raise gen.GenError("author must match [a-z0-9][a-z0-9_]{0,15}")
    if not NAME_RE.match(name or ""):
        raise gen.GenError("name must match [a-z][a-z0-9_]{0,31}")
    module_id = "user.%s.%s" % (author, name)
    directory = os.path.join(user_root, author, name)
    if os.path.exists(directory):
        raise gen.GenError("%s already exists; choose another name" % directory)
    accessor = module_id.replace(".", "_") + ("_input_descriptor" if kind == "input" else "_module_descriptor")
    member = module_id.replace(".", "_")
    if kind == "input":
        state = {"type": camel(name) + "InputState", "bytes": 4}
        state_header = name + "_state.hpp"
        parameters = [{"name": "value", "type": "integer", "default": 0, "minimum": -12, "maximum": 12,
                       "step": 1, "label": "VALUE", "options": [], "saved": True, "ui_only": False,
                       "global_binding": "none"}]
        descriptor = "USER INPUT MODULE"
        header_text = INPUT_HEADER.format(module_id=module_id, accessor=accessor)
        state_text = INPUT_STATE.format(module_id=module_id, type_name=state["type"])
        source_text = INPUT_SOURCE.format(module_id=module_id, name=name, accessor=accessor,
                                          type_name=state["type"], member=member)
    else:
        state = {"type": "EmptyModuleState", "bytes": 1}
        state_header = None
        parameters = []
        descriptor = "USER MODULE"
        header_text = PROCESSOR_HEADER.format(module_id=module_id, accessor=accessor)
        state_text = None
        source_text = PROCESSOR_SOURCE.format(module_id=module_id, name=name, accessor=accessor)
    manifest = {
        "package_format": 1,
        "module_id": module_id,
        "scene_key": name[:gen.SCENE_KEY_MAX],
        "display_name": name.replace("_", " ").title(),
        "label": name.upper().replace("_", "")[:gen.LABEL_MAX],
        "kind": kind,
        "module_version": 1,
        "engine_api": gen.parse_engine_api(),
        "descriptor": descriptor,
        "state": state,
        "parameters": parameters,
        "attributes": [],
        "header": name + ".hpp",
        "sources": [name + ".cpp"],
        "provenance": {"origin": "user", "author": author, "license": "MIT"},
        "enabled_in_release": True,
    }
    if state_header:
        manifest["state_header"] = state_header
    os.makedirs(directory)
    with open(os.path.join(directory, "module.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    with open(os.path.join(directory, name + ".hpp"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header_text)
    with open(os.path.join(directory, name + ".cpp"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source_text)
    if state_text is not None:
        with open(os.path.join(directory, state_header), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(state_text)
    return directory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    new = sub.add_parser("new", help="create a user module and regenerate the tables")
    new.add_argument("--kind", required=True, choices=["processor", "input"])
    new.add_argument("--author", required=True)
    new.add_argument("--name", required=True)
    args = parser.parse_args(argv)
    try:
        directory = scaffold(args.kind, args.author, args.name, gen.USER_DIR)
    except gen.GenError as exc:
        for message in exc.messages:
            print("new_module: error: " + message, file=sys.stderr)
        return 1
    code = gen.main([])
    if code != 0:
        # Validation or the generated-file check failed: leave no half-made module behind.
        shutil.rmtree(directory, ignore_errors=True)
        print("new_module: generator rejected the module; removed " + directory, file=sys.stderr)
        return code
    print("new_module: created " + os.path.relpath(directory, gen.REPO).replace("\\", "/"))
    return code


if __name__ == "__main__":
    sys.exit(main())
