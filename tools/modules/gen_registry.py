#!/usr/bin/env python3
"""Generate the module registry, descriptor table and scene-layout bounds from module.json manifests.

Stdlib only. Inputs are the manifests under:
  firmware/Orbit12/src/modules/core/<id>/module.json       shipped modules (kind processor|input)
  firmware/Orbit12/src/modules/user/<author>/<name>/module.json   user modules compiled into firmware
  modules/examples/<author>/<name>/module.json             sample user modules (test profile only)

Outputs (committed, marked GENERATED):
  firmware/Orbit12/src/modules/generated/registry_generated.cpp        processor registry (release profile)
  firmware/Orbit12/src/modules/generated/input_registry_generated.cpp  input registry (release profile)
  firmware/Orbit12/src/modules/generated/descriptors_generated.hpp     footer descriptor table
  firmware/Orbit12/src/modules/generated/scene_layout_generated.hpp    derived SCN1 bounds (compared in scene_schema.hpp)
  firmware/Orbit12/src/modules/generated/module_inventory.json         core/user id lists (release profile)
  firmware/Orbit12/src/modules/generated/selectors_generated.hpp      UI selector tables (app/ui_model.cpp)
  tests/native/generated/registry_examples.cpp                        processor+input registry including examples
  tests/native/generated/module_sources.cmake                         source lists for the native tests

Usage:
  python -I tools/modules/gen_registry.py            regenerate every output
  python -I tools/modules/gen_registry.py --check    exit 1 when an output is missing or stale
  python -I tools/modules/gen_registry.py --report   print scene/RAM impact (no files written)

Every failure raises GenError with a message that names the file and the rule. The generator never
writes a partially valid set: validation runs before any output is produced.
"""
import argparse
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "firmware", "Orbit12", "src")
CORE_DIR = os.path.join(SRC, "modules", "core")
USER_DIR = os.path.join(SRC, "modules", "user")
EXAMPLE_DIR = os.path.join(REPO, "modules", "examples")
GEN_DIR = os.path.join(SRC, "modules", "generated")
TEST_GEN_DIR = os.path.join(REPO, "tests", "native", "generated")
MARKER = "GENERATED - do not edit. Regenerate with tools/modules/gen_registry.py."

# ---- configuration -------------------------------------------------------------------------
PACKAGE_FORMAT = 1
LABEL_MAX = 7                  # widest shipped selector label (CHRDWLK, NOTEWLK)
DESCRIPTOR_MAX = 17            # module_descriptors.hpp, footer cells
SCENE_KEY_MAX = 10             # kSceneMaxModuleIdChars (SCN1 module id field)
PARAM_NAME_MAX = 21            # kSceneMaxFieldNameChars (SCN1 field name)
ATTR_NAME_MAX = 11             # kSceneMaxAttrNameChars
ATTR_ELEMENTS_MAX = 12         # kSceneMaxAttrValues
ATTR_ROWS_PER_MODULE_MAX = 8   # kSceneMaxAttrsPerModule
CHAIN_SLOTS = 6                # chain processor slots (chain.hpp asserts 6 * sizeof(ModuleState))
CHAIN_STATE_BUDGET = 128 * 1024
STATE_BYTES_CAP = CHAIN_STATE_BUDGET // CHAIN_SLOTS
INPUT_BANKS = 6                # SCN1 BANK sections; architecture contract (six inputs)
PROCESSOR_INHERITED_PARAMS = 3  # source_mode, operation_mode, out_channel (foundation-schema.md)
INPUT_INHERITED_PARAMS = 2      # operation_mode, out_channel (foundation-schema.md)
# Fixed SCN1 rows that no module manifest describes (foundation-schema.md, "Scene records").
FIXED_HEADER_RECORDS = 1        # header
FIXED_GLOBAL_RECORDS = 7        # seven globals
FIXED_MODULE_ROWS = 10          # kSceneModuleSlots: ten logical slots
FIXED_SET_PARAMS = 5            # SET parameters
FIXED_OUT_PARAMS = 2            # OUT parameters
FIXED_END_RECORDS = 1           # END
FIXED_SETTINGS_PARAMS = 0       # SETTINGS has none
HEADROOM_LINES = 8              # kSceneHeadroomLines
PROCESSOR_SLOTS = 6             # six processor slots
RELEASE_FLASH_BUDGET = 409600   # build-firmware.sh ORBIT_FLASH_BUDGET default
RELEASE_RAM_BUDGET = 112640     # build-firmware.sh ORBIT_RAM_BUDGET default

# Reference values, taken from the hand-written scene_schema.hpp before this generator existed.
# The generator must derive exactly these from the core manifests (checked in --check and tests).
CORE_EXPECTED = {
    "scene_base_record_lines": 159,
    "scene_bank_parameter_lines": 58,
    "scene_bank_attribute_lines": 12,
    "scene_bank_values": 170,
    "scene_input_bank_lines": 76,
    "scene_max_bytes": 38637,
    "scene_max_line_chars": 157,
    "max_params_per_module": 17,
    "max_attrs_per_module": 8,
    "max_attr_values": 12,
    "max_field_name_chars": 21,
    "max_attr_name_chars": 11,
    "max_module_id_chars": 10,
    "processor_param_max": 15,
    "processor_attr_rows_max": 3,
    "input_layout": [  # (scene id, persisted parameters, attribute values, offset) in scene order
        ("note", 6, 0, 0),
        ("chords", 13, 0, 6),
        ("drum", 6, 0, 19),
        ("matrix", 17, 96, 25),
        ("note_walk", 5, 0, 138),
        ("chord_walk", 11, 16, 143),
    ],
    "scene_max_bytes_limit": 38637,  # kSceneMaxBytes: the derived bound must not exceed it
    "scene_max_line_bytes": 159,
}

ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
AUTHOR_RE = re.compile(r"^[a-z0-9][a-z0-9_]{0,15}$")
USER_ID_RE = re.compile(r"^user\.([a-z0-9][a-z0-9_]{0,15})\.([a-z][a-z0-9_]*)$")
LABEL_RE = re.compile(r"^[A-Z0-9]+$")
C_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PARAM_TYPES = ("integer", "decimal", "enumeration", "action", "boolean", "text")
ATTR_TYPES = ("integer", "decimal", "boolean", "byte")
BINDINGS = ("none", "octave", "key_offset")
FLOAT_MAX = 3.4028234663852886e38  # largest finite binary32 (ParamSpec decimal ranges)
INT_MIN = -(2 ** 31)
INT_MAX = 2 ** 31 - 1
REQUIRED_KEYS = ("package_format", "module_id", "scene_key", "display_name", "label", "kind",
                 "module_version", "engine_api", "descriptor", "state", "parameters", "attributes",
                 "header", "provenance")


class GenError(Exception):
    def __init__(self, messages):
        if isinstance(messages, str):
            messages = [messages]
        self.messages = list(messages)
        super().__init__("\n".join(self.messages))


def read_text(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def parse_font_chars():
    text = read_text(os.path.join(SRC, "app", "renderer.cpp"))
    match = re.search(r'kFontChars\[\]\s*=\s*"((?:[^"\\]|\\.)*)";', text)
    if not match:
        raise GenError("renderer.cpp: kFontChars not found")
    raw = match.group(1)
    return raw.replace("\\\\", "\\").replace('\\"', '"').replace("\\'", "'")


def parse_engine_api():
    text = read_text(os.path.join(SRC, "app", "build_info.hpp"))
    match = re.search(r"kEngineApi\s*=\s*(\d+)", text)
    if not match:
        raise GenError("build_info.hpp: kEngineApi not found")
    return int(match.group(1))


def parse_release_budgets():
    text = read_text(os.path.join(REPO, "tools", "arduino", "build-firmware.sh"))
    flash = re.search(r"ORBIT_FLASH_BUDGET:-(\d+)", text)
    ram = re.search(r"ORBIT_RAM_BUDGET:-(\d+)", text)
    if not flash or not ram:
        raise GenError("build-firmware.sh: ORBIT_FLASH_BUDGET / ORBIT_RAM_BUDGET defaults not found")
    return int(flash.group(1)), int(ram.group(1))


def parse_selector_order():
    text = read_text(os.path.join(SRC, "app", "ui_model.cpp"))
    result = {}
    for name in ("kProcessorSelectors", "kInputSelectors"):
        block = re.search(name + r"\[\] = \{(.*?)\};", text, re.S)
        if not block:
            raise GenError("ui_model.cpp: " + name + " not found")
        result[name] = [m.group(1) for m in re.finditer(r'\{"([a-z_]+)", "[A-Z0-9]+"\}', block.group(1))]
    return result


CORE_STATE_TYPES = frozenset()  # set by load_modules from the core manifests


class Module:
    def __init__(self, data, path, origin, dirname):
        self.data = data
        self.path = path
        self.origin = origin  # core | user | example
        self.dirname = dirname
        self.id = data.get("module_id")
        self.kind = data.get("kind")

    @property
    def scene_key(self):
        return self.data["scene_key"]

    @property
    def accessor(self):
        base = self.id.replace(".", "_")
        return base + ("_input_descriptor" if self.kind == "input" else "_module_descriptor")

    @property
    def persisted_params(self):
        return [p for p in self.data["parameters"] if p.get("saved") and not p.get("ui_only")]

    @property
    def scene_params(self):
        inherited = PROCESSOR_INHERITED_PARAMS if self.kind == "processor" else INPUT_INHERITED_PARAMS
        return len(self.persisted_params) + inherited

    @property
    def attr_rows(self):
        return len(self.data["attributes"])

    @property
    def attr_values(self):
        return sum(a["element_count"] for a in self.data["attributes"])

    @property
    def state_bytes(self):
        return self.data["state"]["bytes"]

    def state_header_file(self):
        return os.path.join(module_dir_of(self), self.data["state_header"])

    def header_path(self):
        """Core headers are relative to firmware/Orbit12/src; user and example headers to the module directory."""
        if self.origin == "core":
            return os.path.join(SRC, self.data["header"])
        return os.path.join(module_dir_of(self), self.data["header"])


def _validate_param(module, index, param, errors, names):
    where = module.path + ": parameters[%d]" % index
    name = param.get("name")
    if not isinstance(name, str) or not ID_RE.match(name) or len(name) > PARAM_NAME_MAX:
        errors.append("%s: name must match [a-z][a-z0-9_]* and be <= %d characters" % (where, PARAM_NAME_MAX))
        return
    if name in names:
        errors.append("%s: duplicate parameter name '%s'" % (where, name))
    names.add(name)
    ptype = param.get("type")
    if ptype not in PARAM_TYPES:
        errors.append("%s: type must be one of %s" % (where, ", ".join(PARAM_TYPES)))
        return
    if param.get("global_binding", "none") not in BINDINGS:
        errors.append("%s: global_binding must be one of %s" % (where, ", ".join(BINDINGS)))
    if ptype in ("integer", "enumeration"):
        for key in ("default", "minimum", "maximum", "step"):
            value = param.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or not (INT_MIN <= value <= INT_MAX):
                errors.append("%s: %s must be an int32 integer" % (where, key))
                return
        if not (param["minimum"] <= param["default"] <= param["maximum"]):
            errors.append("%s: need minimum <= default <= maximum" % where)
        if param["step"] < 1:
            errors.append("%s: step must be >= 1" % where)
    elif ptype == "decimal":
        for key in ("default", "minimum", "maximum", "step"):
            value = param.get(key)
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value) or abs(value) > FLOAT_MAX * (1 + 1e-6):  # manifests print FLT_MAX with 9 digits
                errors.append("%s: %s must be a finite number" % (where, key))
                return
        if not (param["minimum"] <= param["default"] <= param["maximum"]):
            errors.append("%s: need minimum <= default <= maximum" % where)
    elif ptype == "boolean":
        values = [param.get(k) for k in ("default", "minimum", "maximum", "step")]
        if not all(isinstance(v, bool) for v in values):
            errors.append("%s: boolean default, minimum, maximum and step must be JSON true/false" % where)
        elif param.get("minimum") is not False or param.get("maximum") is not True:
            errors.append("%s: boolean range must be minimum false, maximum true" % where)
    elif ptype == "enumeration":
        options = param.get("options") or []
        if not options or len(options) > 255:
            errors.append("%s: enumeration needs 1..255 options" % where)
        if not (0 <= param.get("minimum", 0) and param.get("maximum", 0) < len(options)):
            errors.append("%s: enumeration maximum must index an option" % where)


def validate_module(module, errors):
    data = module.data
    where = module.path
    missing = [key for key in REQUIRED_KEYS if key not in data]
    for key in missing:
        errors.append("%s: missing required key '%s'" % (where, key))
    if missing:
        return
    if data["package_format"] != PACKAGE_FORMAT:
        errors.append("%s: package_format must be %d" % (where, PACKAGE_FORMAT))
    if data["kind"] not in ("processor", "input"):
        errors.append("%s: kind must be processor or input" % where)
    if data["engine_api"] != ENGINE_API:
        errors.append("%s: engine_api %s does not match firmware kEngineApi %d" % (where, data["engine_api"], ENGINE_API))
    version = data["module_version"]
    if not isinstance(version, int) or not (1 <= version <= 65535):
        errors.append("%s: module_version must be an integer 1..65535" % where)
    # identity
    if module.origin == "core":
        if not ID_RE.match(module.id or "") or module.id != module.dirname:
            errors.append("%s: core module_id must equal its directory name and match [a-z][a-z0-9_]*" % where)
    else:
        match = USER_ID_RE.match(module.id or "")
        if not match:
            errors.append("%s: user module_id must be user.<author>.<name> with [a-z0-9_]" % where)
        else:
            if match.group(1) + "/" + match.group(2) != module.dirname:
                errors.append("%s: module_id %s does not match directory %s" % (where, module.id, module.dirname))
            if module.origin == "user" and data["provenance"].get("author") != match.group(1):
                errors.append("%s: provenance.author must equal the module_id author" % where)
    scene_key = data["scene_key"]
    if not re.match(r"^[a-z0-9_]{1,%d}$" % SCENE_KEY_MAX, scene_key or ""):
        errors.append("%s: scene_key must be [a-z0-9_]{1,%d}" % (where, SCENE_KEY_MAX))
    if module.origin == "core" and scene_key != module.id:
        errors.append("%s: core scene_key must equal module_id (existing scenes depend on it)" % where)
    label = data["label"]
    if not LABEL_RE.match(label or "") or len(label) > LABEL_MAX:
        errors.append("%s: label must be [A-Z0-9]+ and <= %d characters" % (where, LABEL_MAX))
    descriptor = data["descriptor"]
    if not isinstance(descriptor, str) or not (1 <= len(descriptor) <= DESCRIPTOR_MAX):
        errors.append("%s: descriptor must be 1..%d characters" % (where, DESCRIPTOR_MAX))
    else:
        bad = sorted(set(c for c in descriptor if c not in FONT_CHARS))
        if bad:
            errors.append("%s: descriptor has characters not drawable by the font: %r" % (where, "".join(bad)))
    state = data["state"]
    if not C_IDENT_RE.match(state.get("type", "")):
        errors.append("%s: state.type must be a C identifier" % where)
    if not isinstance(state.get("bytes"), int) or not (1 <= state["bytes"] <= STATE_BYTES_CAP):
        errors.append("%s: state.bytes must be 1..%d (chain state budget / %d slots)" % (where, STATE_BYTES_CAP, CHAIN_SLOTS))
    names = set()
    for index, param in enumerate(data["parameters"]):
        _validate_param(module, index, param, errors, names)
    attr_names = set()
    for index, attr in enumerate(data["attributes"]):
        where_attr = "%s: attributes[%d]" % (where, index)
        name = attr.get("name", "")
        if not ID_RE.match(name) or len(name) > ATTR_NAME_MAX:
            errors.append("%s: name must match [a-z][a-z0-9_]* and be <= %d characters" % (where_attr, ATTR_NAME_MAX))
        if name in attr_names:
            errors.append("%s: duplicate attribute name '%s'" % (where_attr, name))
        attr_names.add(name)
        if attr.get("type") not in ATTR_TYPES:
            errors.append("%s: type must be one of %s" % (where_attr, ", ".join(ATTR_TYPES)))
        count = attr.get("element_count")
        if not isinstance(count, int) or not (1 <= count <= ATTR_ELEMENTS_MAX):
            errors.append("%s: element_count must be 1..%d" % (where_attr, ATTR_ELEMENTS_MAX))
    if len(data["attributes"]) > ATTR_ROWS_PER_MODULE_MAX:
        errors.append("%s: at most %d attributes per module" % (where, ATTR_ROWS_PER_MODULE_MAX))
    if module.scene_params > (CORE_EXPECTED["max_params_per_module"] if module.kind == "input" else 99):
        errors.append("%s: scene parameter count %d exceeds the input bound" % (where, module.scene_params))
    if module.kind == "processor" and module.scene_params > CORE_EXPECTED["processor_param_max"]:
        errors.append("%s: processor scene parameters %d exceed the SCN1 slot bound %d (raise the bound only with an SCN1 decision)"
                      % (where, module.scene_params, CORE_EXPECTED["processor_param_max"]))
    if module.kind == "processor" and module.attr_rows > CORE_EXPECTED["processor_attr_rows_max"]:
        errors.append("%s: processor attribute rows %d exceed the SCN1 slot bound %d" % (where, module.attr_rows, CORE_EXPECTED["processor_attr_rows_max"]))
    if not os.path.isfile(module.header_path()):
        errors.append("%s: header %s not found" % (where, module.header_path()))
    if module.origin != "core" and data["state"]["type"] not in CORE_STATE_TYPES and not data.get("state_header"):
        errors.append("%s: state_header is required: %s is not a core state type" % (where, data["state"]["type"]))
    elif module.origin != "core" and data.get("state_header") and not os.path.isfile(module.state_header_file()):
        errors.append("%s: state_header %s not found" % (where, data["state_header"]))
    if module.origin == "core":
        group = data.get("source_group", module.id)
        if not os.path.isdir(os.path.join(CORE_DIR, group)):
            errors.append("%s: source_group %s directory not found" % (where, group))
        for source in data.get("sources", []):
            if not os.path.isfile(os.path.join(CORE_DIR, group, source)):
                errors.append("%s: source %s not found in %s" % (where, source, group))
    else:
        for source in data.get("sources", []):
            if os.path.isabs(source) or ".." in source.replace("\\", "/").split("/"):
                errors.append("%s: source %s must be a relative path inside the module directory" % (where, source))
            elif not os.path.isfile(os.path.join(module_dir_of(module), source)):
                errors.append("%s: source %s not found" % (where, source))
    if module.origin == "user" and not isinstance(data.get("enabled_in_release", True), bool):
        errors.append("%s: enabled_in_release must be true or false" % where)


def module_dir_of(module):
    return os.path.dirname(module.path)


def load_modules(profile):
    """profile: 'release' (core + enabled user) or 'test' (core + user + examples)."""
    errors = []
    modules = []
    for dirpath in sorted(os.listdir(CORE_DIR)) if os.path.isdir(CORE_DIR) else []:
        path = os.path.join(CORE_DIR, dirpath, "module.json")
        if os.path.isfile(path):
            modules.append(read_module(path, "core", dirpath, errors))
    global CORE_STATE_TYPES
    CORE_STATE_TYPES = frozenset(m.data["state"]["type"] for m in modules if m.data and m.origin == "core" and "state" in m.data)
    user_roots = [(USER_DIR, "user")]
    if profile == "test":
        user_roots.append((EXAMPLE_DIR, "example"))
    for root, origin in user_roots:
        if not os.path.isdir(root):
            continue
        for author in sorted(os.listdir(root)):
            author_dir = os.path.join(root, author)
            if not os.path.isdir(author_dir):
                continue
            for name in sorted(os.listdir(author_dir)):
                path = os.path.join(author_dir, name, "module.json")
                if os.path.isfile(path):
                    module = read_module(path, origin, author + "/" + name, errors)
                    if module is None:
                        continue
                    if profile == "release" and not module.data.get("enabled_in_release", True):
                        continue
                    modules.append(module)
    if errors:
        raise GenError(errors)
    return modules


def read_module(path, origin, dirname, errors):
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as exc:
        errors.append("%s: cannot read module.json: %s" % (path, exc))
        return None
    module = Module(data, path, origin, dirname)
    if module.id is None:
        errors.append("%s: missing module_id" % path)
        return None
    return module


def validate_set(modules):
    errors = []
    for module in modules:
        validate_module(module, errors)
    ids = {}
    for module in modules:
        if module.id in ids:
            errors.append("%s: duplicate module_id '%s' (also %s)" % (module.path, module.id, ids[module.id]))
        ids[module.id] = module.path
    keys = {}
    for module in modules:
        key = module.scene_key
        if key in keys:
            errors.append("%s: scene_key '%s' clashes with %s; scene keys must be unique across core and user modules"
                          % (module.path, key, keys[key]))
        keys[key] = module.path
    labels = {}
    for module in modules:
        labels.setdefault(module.data["label"], []).append(module)
    for label, group in labels.items():
        if len(group) > 1:
            kinds = sorted(m.kind for m in group)
            if not (kinds == ["input", "processor"] and all(m.origin == "core" for m in group)):
                errors.append("label %s is shared by %s; only the core processor/input pair may share a label"
                              % (label, ", ".join(m.id for m in group)))
    descriptors = {}
    for module in modules:
        if module.id in descriptors:
            pass
    for module in modules:
        if module.origin != "core":
            if not module.data["provenance"].get("license"):
                errors.append("%s: provenance.license is required for user modules" % module.path)
    if errors:
        raise GenError(errors)


def state_budget(modules):
    biggest = max(modules, key=lambda m: m.state_bytes)
    return {"max_state_bytes": biggest.state_bytes, "max_state_module": biggest.id,
            "chain_state_bytes": biggest.state_bytes * CHAIN_SLOTS, "chain_state_budget": CHAIN_STATE_BUDGET}


def check_scene_and_state(derived, budget, errors):
    if derived["scene_max_bytes"] > CORE_EXPECTED["scene_max_bytes_limit"]:
        errors.append("scene bound %d bytes exceeds kSceneMaxBytes %d; the new module needs an SCN1 decision"
                      % (derived["scene_max_bytes"], CORE_EXPECTED["scene_max_bytes_limit"]))
    if budget["chain_state_bytes"] > budget["chain_state_budget"]:
        errors.append("chain state %d bytes exceeds the %d byte budget" % (budget["chain_state_bytes"], budget["chain_state_budget"]))


def verify_core_equality(modules, derived):
    errors = []
    expected = CORE_EXPECTED
    for key in ("scene_base_record_lines", "scene_bank_parameter_lines", "scene_bank_attribute_lines",
                "scene_bank_values", "scene_input_bank_lines", "scene_max_bytes", "scene_max_line_chars",
                "max_params_per_module", "max_attrs_per_module", "max_attr_values", "max_field_name_chars",
                "max_attr_name_chars", "max_module_id_chars", "processor_param_max", "processor_attr_rows_max"):
        if derived[key] != expected[key]:
            errors.append("derived %s = %d, expected %d (hand value)" % (key, derived[key], expected[key]))
    if [tuple(x) for x in derived["input_layout"]] != [tuple(x) for x in expected["input_layout"]]:
        errors.append("derived input layout %r differs from kSceneInputLayout %r"
                      % (derived["input_layout"], expected["input_layout"]))
    return errors


# ---- emitters ----------------------------------------------------------------------------------
def rel_include(absolute_header, from_dir):
    return os.path.relpath(absolute_header, from_dir).replace("\\", "/")


def emit_sources(test_modules, release_modules):
    """CMake lists for tests/native. Core and release user sources are linked wherever the release
    registry is linked; test-profile user sources (examples included) only where the test registry is."""
    def core_list(modules):
        out = []
        for module in modules:
            if module.origin != "core":
                continue
            group = module.data.get("source_group", module.id)
            for source in module.data.get("sources", []):
                rel = "firmware/Orbit12/src/modules/core/%s/%s" % (group, source)
                if rel not in out:
                    out.append(rel)
        return out

    def user_list(modules):
        out = []
        for module in modules:
            if module.origin == "core":
                continue
            for source in module.data.get("sources", []):
                out.append(os.path.relpath(os.path.join(module_dir_of(module), source), REPO).replace("\\", "/"))
        return out

    # Paths are repository-relative, resolved from this generated file's directory.
    base = "${CMAKE_CURRENT_LIST_DIR}/../../.."
    lines = ["# " + MARKER, "# Module source lists for tests/native (repository-relative, see gen_registry.py)."]
    for name, items in (("ORBIT_MODULE_CORE_SOURCES", core_list(test_modules)),
                        ("ORBIT_MODULE_RELEASE_USER_SOURCES", user_list(release_modules)),
                        ("ORBIT_MODULE_USER_SOURCES", user_list(test_modules))):
        lines.append("set(%s" % name)
        lines += ["  %s/%s" % (base, item) for item in sorted(items)]
        lines.append(")")
    return "\n".join(lines) + "\n"


def _guarded(lines, module, guard):
    """Wrap a user module's lines so native (core-only) builds leave them out."""
    if guard and module.origin != "core":
        return ["#ifndef ORBIT_NATIVE_CORE_ONLY"] + lines + ["#endif"]
    return lines


def emit_registry(processors, path_dir, guard_user):
    """Processor registry. guard_user: release output, where native builds compile the shipped core only."""
    lines = ["// " + MARKER, "// Source: module.json manifests (core order, then user modules sorted by id).",
             "// Native tests define ORBIT_NATIVE_CORE_ONLY (tests/native/CMakeLists.txt): user modules are then left out.",
             "#include <cstdint>",
             "#include <cstring>",
             '#include "%s"' % rel_include(os.path.join(SRC, "domain", "types.hpp"), path_dir),
             '#include "%s"' % rel_include(os.path.join(SRC, "modules", "common", "registry.hpp"), path_dir),
             '#include "%s"' % ("module_counts_generated.hpp" if guard_user else "module_counts.hpp"),
             ""]
    for header in sorted(set(m.header_path() for m in processors)):
        include = ['#include "%s"' % rel_include(header, path_dir)]
        owners = [m for m in processors if m.header_path() == header]
        lines += _guarded(include, owners[0], guard_user and all(m.origin != "core" for m in owners))
    lines += ["", "namespace orbit {"]
    for module in processors:
        lines += _guarded(["const ModuleDescriptor& %s() noexcept;" % module.accessor], module, guard_user)
    lines += ["const ModuleDescriptor* module_at(std::uint8_t index) noexcept {", "  switch (index) {"]
    for index, module in enumerate(processors):
        lines += _guarded(["    case %d: return &%s();" % (index, module.accessor)], module, guard_user)
    lines += ["    default: return nullptr;", "  }", "}",
              "std::uint8_t module_count() noexcept { return kGeneratedProcessorCount; }"]
    # SCN1 stores scene_key (<= 10 chars); the registry maps it to and from the module_id.
    lines += ["namespace {", "struct SceneKeyRow {", "  const char* module_id;", "  const char* scene_key;", "};",
              "constexpr SceneKeyRow kSceneKeyRows[] = {"]
    for module in processors:
        lines += _guarded(['    {"%s", "%s"},' % (module.id, module.scene_key)], module, guard_user)
    lines += ["};", "}  // namespace",
              "const char* module_scene_key(const char* module_id) noexcept {",
              "  if (module_id == nullptr) return nullptr;",
              "  for (const SceneKeyRow& row : kSceneKeyRows) {",
              "    if (std::strcmp(row.module_id, module_id) == 0) return row.scene_key;",
              "  }",
              "  return module_id;  // fixed slots (set, output, settings) and inputs keep their names",
              "}",
              "const char* module_id_for_scene_key(const char* scene_key) noexcept {",
              "  if (scene_key == nullptr) return nullptr;",
              "  for (const SceneKeyRow& row : kSceneKeyRows) {",
              "    if (std::strcmp(row.scene_key, scene_key) == 0) return row.module_id;",
              "  }",
              "  return scene_key;",
              "}",
              "}  // namespace orbit", ""]
    return "\n".join(lines)


def member_name(module):
    """Union member name: the module id with dots replaced (core ids are already C identifiers)."""
    return module.id.replace(".", "_")


def state_header_path(module):
    return os.path.join(module_dir_of(module), module.data["state_header"]) if module.origin != "core" else None


def emit_state_union(modules, storage_name, first_member, max_name, title, guard_user):
    """Union of per-module state structs (ModuleStorage / InputStorage) and its compile-time checks.

    Core state structs stay in modules/common (hand-written); user state headers are included here and must
    include only domain/types.hpp, so the include graph has no cycle. Native core-only builds leave user
    members out (ORBIT_NATIVE_CORE_ONLY), so their size is the shipped core size."""
    lines = ["// " + MARKER, "// " + title,
             "// Include modules/common/module_state.hpp or input_state.hpp (this file depends on the core structs there).",
             "#pragma once", "#include <cstddef>", "#include <cstdint>", ""]
    user = [m for m in modules if m.origin != "core" and m.data.get("state_header")]
    for module in user:
        lines.append('#include "%s"' % rel_include(state_header_path(module), GEN_DIR))
    lines += ["", "namespace orbit {", "union %s {" % storage_name]
    for module in modules:
        lines += _guarded(["    %s %s;" % (module.data["state"]["type"], member_name(module))], module, guard_user)
    lines += ["    constexpr %s() : %s{} {}" % (storage_name, first_member), "};"]
    for module in modules:
        lines += _guarded([
            'static_assert(sizeof(%s) == %d, "%s: state.bytes in module.json disagrees with sizeof");'
            % (module.data["state"]["type"], module.state_bytes, module.id)], module, guard_user)
    core_max = max([m.state_bytes for m in modules if m.origin == "core"] or [0])
    release_max = max([m.state_bytes for m in modules] or [0])
    if guard_user:
        lines += ["#ifdef ORBIT_NATIVE_CORE_ONLY",
                  "inline constexpr std::size_t %s = %d;  // shipped core only (native tests)" % (max_name, core_max),
                  "#else",
                  "inline constexpr std::size_t %s = %d;  // core plus enabled user modules" % (max_name, release_max),
                  "#endif"]
    else:
        # Every module compiled into the tree is in the union, in every build (native tests included).
        lines.append("inline constexpr std::size_t %s = %d;  // core plus modules compiled into the tree" % (max_name, release_max))
    lines += ["static_assert(sizeof(%s) >= %s, \"union smaller than a member\");" % (storage_name, max_name),
              "static_assert(sizeof(%s) - %s < alignof(%s), \"union padding exceeds its alignment\");"
              % (storage_name, max_name, storage_name),
              "static_assert(alignof(%s) <= alignof(std::int64_t), \"state alignment above the chain budget\");"
              % storage_name,
              "}  // namespace orbit", ""]
    return "\n".join(lines)

def inputs_in_scene_order(modules):
    """Input modules in selector order: core inputs by selector_index, then user inputs sorted by id.
    The scene layout (bank order) follows this order."""
    core = sorted([m for m in modules if m.kind == "input" and m.origin == "core"],
                  key=lambda m: m.data["selector_index"])
    user = sorted([m for m in modules if m.kind == "input" and m.origin != "core"], key=lambda m: m.id)
    return core + user


def ordered(modules):
    """Returns (processors, inputs in registry order, inputs in selector order, user processors).
    Core order is preserved (registry_index, selector_index); user modules are appended in sorted id order."""
    core = [m for m in modules if m.origin == "core"]
    processors = sorted([m for m in core if m.kind == "processor"], key=lambda m: m.data["registry_index"])
    core_inputs = sorted([m for m in core if m.kind == "input"], key=lambda m: m.data["registry_index"])
    user_processors = sorted([m for m in modules if m.origin != "core" and m.kind == "processor"], key=lambda m: m.id)
    user_inputs = sorted([m for m in modules if m.origin != "core" and m.kind == "input"], key=lambda m: m.id)
    processors = processors + user_processors
    inputs = core_inputs + user_inputs
    return processors, inputs, inputs_in_scene_order(modules), user_processors


def derive_scene(modules):
    """SCN1 bounds derived from manifests plus the fixed SCN1 rows (foundation-schema.md)."""
    processors = [m for m in modules if m.kind == "processor"]
    inputs = [m for m in modules if m.kind == "input"]
    scene_inputs = inputs_in_scene_order(modules)
    input_layout = []
    offset = 0
    for module in scene_inputs:
        params = len(module.persisted_params) + INPUT_INHERITED_PARAMS
        values = module.attr_values
        input_layout.append((module.scene_key, params, values, offset))
        offset += params + values
    bank_parameter_lines = sum(layout[1] for layout in input_layout)
    bank_attribute_lines = sum(m.attr_rows for m in scene_inputs)
    bank_values = offset
    input_banks = len(scene_inputs)
    input_bank_lines = input_banks + bank_parameter_lines + bank_attribute_lines
    proc_param_max = max([m.scene_params for m in processors] or [0])
    proc_attr_max = max([m.attr_rows for m in processors] or [0])
    in_param_max = max([m.scene_params for m in inputs] or [0])
    in_attr_max = max([m.attr_rows for m in inputs] or [0])
    base_records = (FIXED_HEADER_RECORDS + FIXED_GLOBAL_RECORDS + FIXED_MODULE_ROWS + FIXED_SET_PARAMS
                    + FIXED_OUT_PARAMS + FIXED_SETTINGS_PARAMS + FIXED_END_RECORDS
                    + in_param_max + PROCESSOR_SLOTS * proc_param_max
                    + in_attr_max + PROCESSOR_SLOTS * proc_attr_max)
    names = [p["name"] for m in modules for p in m.data["parameters"]]
    attr_names = [a["name"] for m in modules for a in m.data["attributes"]]
    field_max = max(len(n) for n in names) if names else 0
    attr_name_max = max(len(n) for n in attr_names) if attr_names else 0
    attr_values_max = max([a["element_count"] for m in modules for a in m.data["attributes"]] or [0])
    # SCN1 line sizes (scene_schema.hpp): attribute line = 2 + name + 1 + values, values = n*11 + (n-1);
    # parameter line = 2 + field name + 1 + 15 (finite float, nine significant digits).
    attr_line = 2 + attr_name_max + 1 + (attr_values_max * 11 + max(attr_values_max - 1, 0))
    param_line = 2 + field_max + 1 + 15
    max_line_chars = max(attr_line, param_line)
    max_line_bytes = max_line_chars + 2  # CRLF
    scene_bytes = (base_records + input_bank_lines + HEADROOM_LINES) * max_line_bytes
    return {
        "scene_base_record_lines": base_records,
        "scene_bank_parameter_lines": bank_parameter_lines,
        "scene_bank_attribute_lines": bank_attribute_lines,
        "scene_bank_values": bank_values,
        "scene_input_banks": input_banks,
        "scene_input_bank_lines": input_bank_lines,
        "scene_max_bytes": scene_bytes,
        "scene_max_line_chars": max_line_chars,
        "scene_max_line_bytes": max_line_bytes,
        "max_params_per_module": in_param_max,
        "max_attrs_per_module": in_attr_max,
        "max_attr_values": attr_values_max,
        "max_field_name_chars": field_max,
        "max_attr_name_chars": attr_name_max,
        "max_module_id_chars": max(len(m.scene_key) for m in modules),
        "processor_param_max": proc_param_max,
        "processor_attr_rows_max": proc_attr_max,
        "input_layout": input_layout,
    }


def emit_input_registry(inputs, path_dir):
    """Input registry in registry order (core inputs first, user inputs sorted by id)."""
    lines = ["// " + MARKER, "// Source: module.json manifests (registry order; user inputs after the core inputs).",
             "#include <cstdint>",
             '#include "%s"' % rel_include(os.path.join(SRC, "modules", "common", "input_registry.hpp"), path_dir),
             '#include "module_counts_generated.hpp"']
    for header in sorted(set(m.header_path() for m in inputs)):
        owners = [m for m in inputs if m.header_path() == header]
        lines += _guarded(['#include "%s"' % rel_include(header, path_dir)], owners[0],
                          all(m.origin != "core" for m in owners))
    lines += ["", "namespace orbit {"]
    for module in inputs:
        lines += _guarded(["const InputDescriptor& %s() noexcept;" % module.accessor], module, True)
    lines += ["const InputDescriptor* input_module_at(std::uint8_t index) noexcept {", "  switch (index) {"]
    for index, module in enumerate(inputs):
        lines += _guarded(["    case %d: return &%s();" % (index, module.accessor)], module, True)
    lines += ["    default: return nullptr;", "  }", "}",
              "std::uint8_t input_module_count() noexcept { return kGeneratedInputCount; }",
              "}  // namespace orbit", ""]
    return "\n".join(lines)


def emit_descriptors(selector_inputs, processors):
    rows = [(m, [m.id, m.data["label"], m.data["descriptor"]]) for m in selector_inputs]
    rows += [(m, [m.id, m.data["label"], m.data["descriptor"]]) for m in processors]
    lines = ["// " + MARKER, "// Footer descriptions (<= 17 chars). Included by app/module_descriptors.hpp.",
             "// Rows: inputs in selector order, then processors in registry order, user modules last.",
             "inline constexpr std::size_t kModuleDescriptionCount = kGeneratedProcessorCount + kGeneratedInputCount;",
             "inline constexpr ModuleDescription kModuleDescriptions[kModuleDescriptionCount] = {"]
    for module, row in rows:
        lines += _guarded(['    {"%s", "%s", "%s"},' % tuple(row)], module, True)
    lines.append("};")
    return "\n".join(lines) + "\n"

LITTLEFS_PARTITION_BYTES = 1024 * 1024       # 1 MiB scene partition (docs/migration/storage.md)
LITTLEFS_SCENE_SLOTS = 16                   # Scene01..Scene16
LITTLEFS_TEMP_AND_BACKUP = 2                # <name>.tmp and <name>.bak during a commit
LITTLEFS_BUDGET_FRACTION = 0.70             # fail when the worst case exceeds 70 % of the partition
SCENE_FILE_BUDGET = int(LITTLEFS_PARTITION_BYTES * LITTLEFS_BUDGET_FRACTION
                        / (LITTLEFS_SCENE_SLOTS + LITTLEFS_TEMP_AND_BACKUP))   # 40,777 bytes per scene file
DTO_BANK_VALUE_BYTES = 4                    # SceneScalar (int32 / binary32 / bool) upper bound
DTO_BUDGET_BYTES = 16 * 1024                # bank values held in RAM by SceneDto and the controller


def _profile(name, core_value, release_value):
    """A constant that differs only for the native core-only profile (ORBIT_NATIVE_CORE_ONLY)."""
    return ["#ifdef ORBIT_NATIVE_CORE_ONLY",
            "inline constexpr std::size_t %s = %d;" % (name, core_value),
            "#else",
            "inline constexpr std::size_t %s = %d;" % (name, release_value),
            "#endif"]


def emit_selectors(processors, selector_inputs):
    lines = ["// " + MARKER, "// UI selector tables (app/ui_model.cpp): processors in registry order, inputs in selector order.",
             "constexpr SelectorEntry kProcessorSelectors[] = {"]
    for module in processors:
        lines += _guarded(['    {"%s", "%s"},' % (module.id, module.data["label"])], module, True)
    lines.append("};")
    lines.append("constexpr SelectorEntry kInputSelectors[] = {")
    for module in selector_inputs:
        lines += _guarded(['    {"%s", "%s"},' % (module.id, module.data["label"])], module, True)
    lines.append("};")
    return "\n".join(lines) + "\n"


def emit_counts(processors, inputs, core_processor_count, core_input_count, guard_user):
    """guard_user: release header; native builds (ORBIT_NATIVE_CORE_ONLY) count the shipped core only."""
    lines = ["// " + MARKER, "// Module counts (kGeneratedProcessorCount is the registry size).",
             "#pragma once", "#include <cstdint>", "", "namespace orbit {",
             "inline constexpr std::uint8_t kGeneratedCoreProcessorCount = %d;" % core_processor_count]
    if guard_user:
        lines += ["#ifdef ORBIT_NATIVE_CORE_ONLY",
                  "inline constexpr std::uint8_t kGeneratedProcessorCount = %d;  // native tests: shipped core only" % core_processor_count,
                  "inline constexpr std::uint8_t kGeneratedInputCount = %d;" % core_input_count,
                  "#else",
                  "inline constexpr std::uint8_t kGeneratedProcessorCount = %d;  // core plus enabled user modules" % len(processors),
                  "inline constexpr std::uint8_t kGeneratedInputCount = %d;" % len(inputs),
                  "#endif"]
    else:
        lines += ["inline constexpr std::uint8_t kGeneratedProcessorCount = %d;" % len(processors),
                  "inline constexpr std::uint8_t kGeneratedInputCount = %d;" % len(inputs)]
    lines += ["}  // namespace orbit", ""]
    return "\n".join(lines)


def emit_inventory(processors, inputs):
    core = [m.id for m in processors if m.origin == "core"] + [m.id for m in inputs if m.origin == "core"]
    user = [m.id for m in processors if m.origin != "core"] + [m.id for m in inputs if m.origin != "core"]
    counts = {"core_count": len(core), "user_count": len(user), "total_count": len(core) + len(user)}
    return json.dumps(dict({"marker": MARKER, "core": core, "user": user}, **counts), indent=2) + "\n"


def emit_scene_layout(derived, core_derived):
    """Scene bounds (release and core-only variants). scene_schema.hpp uses these names; in native
    core-only builds the static asserts there compare them with the six-input baseline."""
    lines = ["// " + MARKER, "// Derived from module.json manifests plus the fixed SCN1 rows (foundation-schema.md).",
             "// Native core-only builds (ORBIT_NATIVE_CORE_ONLY) get the six-input baseline values.",
             "#pragma once", "#include <cstddef>", "#include <cstdint>", "", "namespace orbit {",
             "struct SceneInputLayout {",
             "  const char* id;", "  std::uint8_t parameters;", "  std::uint8_t attribute_values;", "  std::uint8_t offset;", "};"]
    pairs = (("kGeneratedSceneBaseRecordLines", "scene_base_record_lines"),
             ("kGeneratedSceneBankParameterLines", "scene_bank_parameter_lines"),
             ("kGeneratedSceneBankAttributeLines", "scene_bank_attribute_lines"),
             ("kGeneratedSceneBankValues", "scene_bank_values"),
             ("kGeneratedSceneInputBanks", "scene_input_banks"),
             ("kGeneratedSceneInputBankLines", "scene_input_bank_lines"),
             ("kGeneratedSceneMaxBytes", "scene_max_bytes"),
             ("kGeneratedSceneMaxLineChars", "scene_max_line_chars"),
             ("kGeneratedSceneMaxParamsPerModule", "max_params_per_module"),
             ("kGeneratedSceneMaxAttrsPerModule", "max_attrs_per_module"),
             ("kGeneratedSceneMaxAttrValues", "max_attr_values"),
             ("kGeneratedSceneMaxFieldNameChars", "max_field_name_chars"),
             ("kGeneratedSceneMaxAttrNameChars", "max_attr_name_chars"),
             ("kGeneratedSceneMaxModuleIdChars", "max_module_id_chars"),
             ("kGeneratedSceneProcessorParamMax", "processor_param_max"),
             ("kGeneratedSceneProcessorAttrRowsMax", "processor_attr_rows_max"))
    for name, key in pairs:
        lines += _profile(name, core_derived[key], derived[key])
    lines += _profile("kGeneratedSceneDtoBankValueBytes", core_derived["scene_bank_values"] * DTO_BANK_VALUE_BYTES,
                      derived["scene_bank_values"] * DTO_BANK_VALUE_BYTES)
    lines.append("inline constexpr SceneInputLayout kGeneratedSceneInputLayout[kGeneratedSceneInputBanks] = {")
    for module in [m for m in derived["_modules"]]:
        pass
    for scene_id, params, values, offset, origin in derived["input_rows"]:
        lines += _guarded(['    {"%s", %d, %d, %d},' % (scene_id, params, values, offset)],
                          _Origin(origin), True)
    lines += ["};", "}  // namespace orbit", ""]
    return "\n".join(lines)


class _Origin:
    def __init__(self, origin):
        self.origin = origin


def build_outputs(release_modules, test_modules):
    """Returns {absolute path: text} for every generated file."""
    processors, inputs, selector_inputs, user_processors = ordered(release_modules)
    core_modules = [m for m in release_modules if m.origin == "core"]
    core_processors, core_inputs, _, _ = ordered(core_modules)
    outputs = {}
    outputs[os.path.join(GEN_DIR, "registry_generated.cpp")] = emit_registry(processors, GEN_DIR, True)
    outputs[os.path.join(GEN_DIR, "input_registry_generated.cpp")] = emit_input_registry(inputs, GEN_DIR)
    outputs[os.path.join(GEN_DIR, "descriptors_generated.hpp")] = emit_descriptors(selector_inputs, processors)
    derived = derive_scene(release_modules)
    derived["_modules"] = release_modules
    derived["input_rows"] = [(m.scene_key, len(m.persisted_params) + INPUT_INHERITED_PARAMS, m.attr_values, offset, m.origin)
                             for m, offset in _input_offsets(selector_inputs)]
    core_derived = derive_scene(core_modules)
    core_derived["_modules"] = core_modules
    core_derived["input_rows"] = [(m.scene_key, len(m.persisted_params) + INPUT_INHERITED_PARAMS, m.attr_values, offset, m.origin)
                                  for m, offset in _input_offsets(inputs_in_scene_order(core_modules))]
    outputs[os.path.join(GEN_DIR, "scene_layout_generated.hpp")] = emit_scene_layout(derived, core_derived)
    outputs[os.path.join(GEN_DIR, "module_inventory.json")] = emit_inventory(processors, inputs)
    outputs[os.path.join(GEN_DIR, "selectors_generated.hpp")] = emit_selectors(processors, selector_inputs)
    outputs[os.path.join(GEN_DIR, "module_state_generated.hpp")] = emit_state_union(
        processors, "ModuleStorage", "empty", "kGeneratedModuleStateMaxBytes",
        "Processor state union (ModuleState::storage). Generated from module.json state entries.", False)
    outputs[os.path.join(GEN_DIR, "input_state_generated.hpp")] = emit_state_union(
        inputs, "InputStorage", "note", "kGeneratedInputStateMaxBytes",
        "Input state union (InputState::storage). Generated from module.json state entries.", False)
    test_processors, test_inputs, _, _ = ordered(test_modules)
    core_count_test = sum(1 for m in test_processors if m.origin == "core")
    core_inputs_test = sum(1 for m in test_inputs if m.origin == "core")
    outputs[os.path.join(GEN_DIR, "module_counts_generated.hpp")] = emit_counts(
        processors, inputs, sum(1 for m in processors if m.origin == "core"), len(core_inputs), True)
    outputs[os.path.join(TEST_GEN_DIR, "registry_examples.cpp")] = emit_registry(test_processors, TEST_GEN_DIR, False)
    outputs[os.path.join(TEST_GEN_DIR, "module_counts.hpp")] = emit_counts(
        test_processors, test_inputs, core_count_test, core_inputs_test, False)
    outputs[os.path.join(TEST_GEN_DIR, "module_sources.cmake")] = emit_sources(test_modules, release_modules)
    return outputs


def _input_offsets(input_modules):
    out = []
    offset = 0
    for module in input_modules:
        out.append((module, offset))
        offset += len(module.persisted_params) + INPUT_INHERITED_PARAMS + module.attr_values
    return out


def check_budgets(release_modules, derived, budget):
    errors = []
    if derived["scene_max_bytes"] > SCENE_FILE_BUDGET:
        errors.append("scene file bound %d bytes exceeds the LittleFS budget %d (16 scenes + tmp/bak must fit in 70%% of the 1 MiB partition)"
                      % (derived["scene_max_bytes"], SCENE_FILE_BUDGET))
    if derived["scene_bank_values"] * DTO_BANK_VALUE_BYTES > DTO_BUDGET_BYTES:
        errors.append("SceneDto bank values %d bytes exceed the RAM budget %d"
                      % (derived["scene_bank_values"] * DTO_BANK_VALUE_BYTES, DTO_BUDGET_BYTES))
    if budget["chain_state_bytes"] > budget["chain_state_budget"]:
        errors.append("chain state %d bytes exceeds the %d byte budget" % (budget["chain_state_bytes"], budget["chain_state_budget"]))
    input_state = max([m.state_bytes for m in release_modules if m.kind == "input"] or [0])
    if input_state * CHAIN_SLOTS > CHAIN_STATE_BUDGET:
        errors.append("input state %d bytes exceeds the chain state budget" % input_state)
    return errors


def run(args):
    flash_budget, ram_budget = parse_release_budgets()
    if flash_budget != RELEASE_FLASH_BUDGET or ram_budget != RELEASE_RAM_BUDGET:
        raise GenError("generator budgets (%d, %d) differ from build-firmware.sh (%d, %d)"
                       % (RELEASE_FLASH_BUDGET, RELEASE_RAM_BUDGET, flash_budget, ram_budget))
    global ENGINE_API, FONT_CHARS
    ENGINE_API = parse_engine_api()
    FONT_CHARS = parse_font_chars()
    release = load_modules("release")
    test = load_modules("test")
    validate_set(test)
    core = [m for m in release if m.origin == "core"]
    errors = verify_core_equality(core, derive_scene(core))
    derived = derive_scene(release)
    budget = state_budget(release)
    errors += check_budgets(release, derived, budget)
    if errors:
        raise GenError(errors)
    return release, test, derived, budget


def report(release, derived, budget):
    lines = ["Modules: %d release (%d core, %d user)" % (len(release), sum(1 for m in release if m.origin == "core"),
                                                       sum(1 for m in release if m.origin != "core"))]
    lines.append("Scene bound: %d bytes (kSceneMaxBytes %d), base records %d, bank lines %d, values %d"
                 % (derived["scene_max_bytes"], CORE_EXPECTED["scene_max_bytes_limit"], derived["scene_base_record_lines"],
                    derived["scene_input_bank_lines"], derived["scene_bank_values"]))
    lines.append("State: largest %s = %d bytes; chain state %d of %d budget bytes"
                 % (budget["max_state_module"], budget["max_state_bytes"], budget["chain_state_bytes"], budget["chain_state_budget"]))
    lines.append("Flash/globals: enforced by build-firmware.sh (flash %d, RAM %d); build it to measure."
                 % (RELEASE_FLASH_BUDGET, RELEASE_RAM_BUDGET))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail when generated files are missing or stale")
    parser.add_argument("--report", action="store_true", help="print the scene and RAM impact; write nothing")
    args = parser.parse_args(argv)
    try:
        release, test, derived, budget = run(args)
        outputs = build_outputs(release, test)
    except GenError as exc:
        for message in exc.messages:
            print("gen_registry: error: " + message, file=sys.stderr)
        return 1
    if args.report:
        print(report(release, derived, budget))
        return 0
    stale = []
    for path, text in sorted(outputs.items()):
        current = read_text(path) if os.path.isfile(path) else None
        if args.check:
            if current != text:
                stale.append(path)
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)
    if args.check:
        for path in stale:
            print("gen_registry: stale or missing: " + os.path.relpath(path, REPO), file=sys.stderr)
        if stale:
            print("gen_registry: run `python -I tools/modules/gen_registry.py` and commit the result", file=sys.stderr)
            return 1
        print("gen_registry: generated files are current")
        return 0
    print(report(release, derived, budget))
    print("gen_registry: wrote %d files" % len(outputs))
    return 0


FONT_CHARS = ""
ENGINE_API = 1

if __name__ == "__main__":
    sys.exit(main())
