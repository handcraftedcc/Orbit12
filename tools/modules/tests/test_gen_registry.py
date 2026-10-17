"""Tests for tools/modules/gen_registry.py: validation, determinism, --check and the derived SCN1 bounds.

Run from the repository root:  python -I -m unittest discover -s tools/modules/tests
User and example module trees are redirected to temporary directories, so these tests never write
into the repository. The core manifests and the committed generated files are read as they are.
"""
import copy
import importlib.util
import json
import os
import re
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), "gen_registry.py")
_spec = importlib.util.spec_from_file_location("gen_registry", TOOL)
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)

PROCESSOR_HEADER = """#pragma once
#include "modules/common/module_state.hpp"
namespace orbit {
const ModuleDescriptor& user_test_probe_module_descriptor() noexcept;
}
"""
PROCESSOR_SOURCE = """#include "probe.hpp"
namespace orbit {
namespace {
void noop_construct(ModuleState&) noexcept {}
void noop_process(ModuleState&, const ProcessContext&, const NoteBatch& in, NoteBatch& out) noexcept { out = in; }
void noop_reset(ModuleState&, ResetReason) noexcept {}
void noop_destroy(ModuleState&) noexcept {}
void noop_attributes(ModuleState&, AttributeVisitor&) noexcept {}
const ModuleDescriptor kDescriptor{"user.test.probe", 1, nullptr, 0, noop_construct, noop_destroy,
    noop_process, noop_reset, nullptr, noop_attributes};
}  // namespace
const ModuleDescriptor& user_test_probe_module_descriptor() noexcept { return kDescriptor; }
}  // namespace orbit
"""


def valid_manifest(**overrides):
    manifest = {
        "package_format": 1,
        "module_id": "user.test.probe",
        "scene_key": "probe",
        "display_name": "Probe",
        "label": "PROBE",
        "kind": "processor",
        "module_version": 1,
        "engine_api": gen.parse_engine_api(),
        "descriptor": "PROBE PASSES",
        "state": {"type": "EmptyModuleState", "bytes": 1},
        "parameters": [],
        "attributes": [],
        "header": "probe.hpp",
        "sources": ["probe.cpp"],
        "provenance": {"origin": "user", "author": "test", "license": "MIT"},
        "enabled_in_release": True,
    }
    manifest.update(overrides)
    return manifest


def int_param(name, default=0, minimum=0, maximum=10):
    return {"name": name, "type": "integer", "default": default, "minimum": minimum, "maximum": maximum,
            "step": 1, "label": "P", "options": [], "saved": True, "ui_only": False, "global_binding": "none"}


class GeneratorTestCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.mkdtemp(prefix="gen-registry-")
        self.user_dir = os.path.join(self._tmp, "user")
        self.example_dir = os.path.join(self._tmp, "examples")
        self.gen_dir = os.path.join(self._tmp, "gen")
        self.test_gen_dir = os.path.join(self._tmp, "test_gen")
        os.makedirs(self.user_dir)
        os.makedirs(self.example_dir)
        self._saved = {name: getattr(gen, name) for name in ("USER_DIR", "EXAMPLE_DIR", "GEN_DIR", "TEST_GEN_DIR")}
        gen.USER_DIR = self.user_dir
        gen.EXAMPLE_DIR = self.example_dir
        gen.GEN_DIR = self.gen_dir
        gen.TEST_GEN_DIR = self.test_gen_dir

    def tearDown(self):
        for name, value in self._saved.items():
            setattr(gen, name, value)
        shutil.rmtree(self._tmp, ignore_errors=True)

    def add_user(self, manifest, author="test", name="probe", root=None, header=True):
        root = root or self.user_dir
        directory = os.path.join(root, author, name)
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, "module.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        if header:
            with open(os.path.join(directory, "probe.hpp"), "w", encoding="utf-8") as handle:
                handle.write(PROCESSOR_HEADER)
            with open(os.path.join(directory, "probe.cpp"), "w", encoding="utf-8") as handle:
                handle.write(PROCESSOR_SOURCE)
        return directory

    def load_and_validate(self, profile="release"):
        gen.ENGINE_API = gen.parse_engine_api()
        gen.FONT_CHARS = gen.parse_font_chars()
        modules = gen.load_modules(profile)
        gen.validate_set(modules)
        return modules

    def assert_error(self, needle, manifest=None, **kwargs):
        if manifest is not None:
            self.add_user(manifest, **kwargs)
        with self.assertRaises(gen.GenError) as caught:
            modules = self.load_and_validate("test")
            gen.validate_set(modules)
        text = "\n".join(caught.exception.messages)
        self.assertIn(needle, text)


class CoreManifests(unittest.TestCase):
    """The shipped manifests must derive exactly the hand-written SCN1 values."""

    @classmethod
    def setUpClass(cls):
        cls.gen = gen
        gen.ENGINE_API = gen.parse_engine_api()
        gen.FONT_CHARS = gen.parse_font_chars()
        cls.core = [m for m in gen.load_modules("release") if m.origin == "core"]

    def test_core_validates(self):
        gen.validate_set(self.core)

    def test_derived_bounds_equal_hand_values(self):
        derived = gen.derive_scene(self.core)
        self.assertEqual(gen.verify_core_equality(self.core, derived), [])
        self.assertEqual(derived["scene_base_record_lines"], 159)
        self.assertEqual(derived["scene_bank_parameter_lines"], 58)
        self.assertEqual(derived["scene_bank_values"], 170)
        self.assertEqual(derived["scene_max_bytes"], 38637)

    def test_input_layout_is_the_hand_table(self):
        derived = gen.derive_scene(self.core)
        self.assertEqual([tuple(x) for x in derived["input_layout"]], gen.CORE_EXPECTED["input_layout"])

    def test_budgets_match_build_script(self):
        self.assertEqual(gen.parse_release_budgets(), (409600, 112640))

    def test_state_budget_matches_documented_delay_size(self):
        budget = gen.state_budget(self.core)
        self.assertEqual(budget["max_state_module"], "delay")
        self.assertEqual(budget["max_state_bytes"], 632)

    def test_committed_outputs_are_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


class ValidationRules(GeneratorTestCase):
    def test_valid_user_processor_passes_and_registers(self):
        self.add_user(valid_manifest())
        modules = self.load_and_validate("test")
        user = [m for m in modules if m.origin == "user"]
        self.assertEqual([m.id for m in user], ["user.test.probe"])
        processors, _, _, user_processors = gen.ordered(modules)
        self.assertEqual(processors[-1].id, "user.test.probe")
        text = gen.emit_registry(processors, self.test_gen_dir, False)
        self.assertIn("user_test_probe_module_descriptor()", text)
        self.assertIn("module_count() noexcept { return kGeneratedProcessorCount; }", text)
        self.assertIn("#include \"module_counts.hpp\"", text)

    def test_label_too_long(self):
        self.assert_error("label must be", valid_manifest(label="TOOLONGLABEL"))

    def test_descriptor_too_long(self):
        self.assert_error("descriptor must be", valid_manifest(descriptor="X" * 18))

    def test_descriptor_char_not_in_font(self):
        self.assert_error("not drawable", valid_manifest(descriptor="lower case"))

    def test_scene_key_clashes_with_core(self):
        self.assert_error("clashes with", valid_manifest(scene_key="arp"))

    def test_scene_key_too_long(self):
        self.assert_error("scene_key must be", valid_manifest(scene_key="abcdefghijk"))

    def test_module_id_must_be_namespaced(self):
        self.assert_error("user module_id must be", valid_manifest(module_id="probe"))

    def test_module_id_must_match_directory(self):
        self.assert_error("does not match directory", valid_manifest(module_id="user.test.other"))

    def test_duplicate_parameter_name(self):
        self.assert_error("duplicate parameter name",
                          valid_manifest(parameters=[int_param("amount"), int_param("amount")]))

    def test_default_outside_range(self):
        self.assert_error("minimum <= default <= maximum",
                          valid_manifest(parameters=[int_param("amount", default=20, maximum=10)]))

    def test_parameter_name_too_long(self):
        self.assert_error("name must match", valid_manifest(parameters=[int_param("a" * 22)]))

    def test_processor_parameters_over_slot_bound(self):
        params = [int_param("p%d" % i) for i in range(13)]  # 13 persisted + 3 inherited = 16 > 15
        self.assert_error("exceed the SCN1 slot bound", valid_manifest(parameters=params))

    def test_attribute_rows_over_bound(self):
        attrs = [{"name": "a%d" % i, "type": "integer", "element_count": 1} for i in range(9)]
        self.assert_error("at most 8 attributes", valid_manifest(attributes=attrs))

    def test_state_bytes_over_cap(self):
        self.assert_error("state.bytes must be", valid_manifest(state={"type": "EmptyModuleState", "bytes": 99999}))

    def test_engine_api_mismatch(self):
        self.assert_error("engine_api", valid_manifest(engine_api=99))

    def test_user_input_module_is_accepted_with_a_state_header(self):
        manifest = valid_manifest(kind="input", state={"type": "ProbeInputState", "bytes": 4},
                                  state_header="probe_state.hpp", label="PRBIN", scene_key="probein")
        directory = self.add_user(manifest)
        with open(os.path.join(directory, "probe_state.hpp"), "w", encoding="utf-8") as handle:
            handle.write("#pragma once\nnamespace orbit { struct ProbeInputState { int value = 0; }; }\n")
        modules = self.load_and_validate("test")
        self.assertIn("user.test.probe", [m.id for m in modules if m.kind == "input"])

    def test_user_state_type_without_header_is_rejected(self):
        self.assert_error("state_header is required",
                          valid_manifest(kind="input", state={"type": "ProbeInputState", "bytes": 4}, label="PRBIN"))

    def test_shared_label_rejected_for_user_module(self):
        self.assert_error("only the core processor/input pair", valid_manifest(label="ARP"))

    def test_missing_required_key(self):
        manifest = valid_manifest()
        del manifest["descriptor"]
        self.assert_error("missing required key 'descriptor'", manifest)

    def test_boolean_default_must_be_json_boolean(self):
        bad = {"name": "flag", "type": "boolean", "default": 1, "minimum": False, "maximum": True,
               "step": True, "label": "F", "options": [], "saved": True, "ui_only": False, "global_binding": "none"}
        self.assert_error("JSON true/false", valid_manifest(parameters=[bad]))

    def test_missing_source_file(self):
        self.assert_error("source missing.cpp not found", valid_manifest(sources=["missing.cpp"]))

    def test_source_outside_module_directory(self):
        self.assert_error("relative path inside", valid_manifest(sources=["../evil.cpp"]))


class BudgetsAndInputs(GeneratorTestCase):
    def test_scene_file_over_the_littlefs_budget_is_rejected(self):
        core = [m for m in self.load_and_validate("release") if m.origin == "core"]
        saved = gen.SCENE_FILE_BUDGET
        gen.SCENE_FILE_BUDGET = 1000
        try:
            errors = gen.check_budgets(core, gen.derive_scene(core), gen.state_budget(core))
        finally:
            gen.SCENE_FILE_BUDGET = saved
        self.assertTrue(any("LittleFS" in e for e in errors), errors)

    def test_core_scene_file_is_within_the_littlefs_budget(self):
        core = [m for m in self.load_and_validate("release") if m.origin == "core"]
        self.assertEqual(gen.check_budgets(core, gen.derive_scene(core), gen.state_budget(core)), [])

    def test_user_input_adds_a_seventh_bank(self):
        manifest = valid_manifest(kind="input", state={"type": "ProbeInputState", "bytes": 4},
                                  state_header="probe_state.hpp", label="PRBIN", scene_key="probein",
                                  parameters=[int_param("value")])
        directory = self.add_user(manifest)
        with open(os.path.join(directory, "probe_state.hpp"), "w", encoding="utf-8") as handle:
            handle.write("#pragma once\nnamespace orbit { struct ProbeInputState { int value = 0; }; }\n")
        modules = self.load_and_validate("release")
        derived = gen.derive_scene(modules)
        self.assertEqual(len(derived["input_layout"]), 7)
        self.assertEqual(derived["input_layout"][-1][0], "probein")
        # 1 bank header + (1 persisted + 2 inherited) parameter lines; no attribute rows
        self.assertEqual(derived["scene_input_bank_lines"], 76 + 1 + 3)


class Determinism(GeneratorTestCase):
    def test_outputs_are_byte_identical_across_runs(self):
        self.add_user(valid_manifest())
        first = gen.build_outputs(self.load_and_validate("release"), self.load_and_validate("test"))
        second = gen.build_outputs(self.load_and_validate("release"), self.load_and_validate("test"))
        self.assertEqual(first, second)

    def test_user_order_is_sorted_after_core(self):
        self.add_user(valid_manifest(module_id="user.test.zeta", scene_key="zeta", label="ZETA",
                                     header="probe.hpp", sources=["probe.cpp"]), name="zeta")
        self.add_user(valid_manifest(module_id="user.test.alpha", scene_key="alpha", label="ALPHA"),
                      name="alpha")
        modules = self.load_and_validate("test")
        processors, _, _, _ = gen.ordered(modules)
        self.assertEqual([m.id for m in processors[-2:]], ["user.test.alpha", "user.test.zeta"])


class StaleDetection(GeneratorTestCase):
    def test_check_fails_when_an_output_is_stale(self):
        self.add_user(valid_manifest(), name="probe")
        release = self.load_and_validate("release")
        test = self.load_and_validate("test")
        outputs = gen.build_outputs(release, test)
        for path, text in outputs.items():
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)
        self.assertEqual(gen.main(["--check"]), 0)
        with open(next(iter(outputs)), "a", encoding="utf-8") as handle:
            handle.write("// hand edit\n")
        self.assertEqual(gen.main(["--check"]), 1)

    def test_check_fails_when_an_output_is_missing(self):
        self.add_user(valid_manifest(), name="probe")
        self.assertEqual(gen.main(["--check"]), 1)


class ExampleProfile(GeneratorTestCase):
    def test_disabled_examples_are_excluded_from_release_only(self):
        self.add_user(valid_manifest(enabled_in_release=False), root=self.user_dir, name="probe")
        release = self.load_and_validate("release")
        self.assertEqual([m for m in release if m.origin != "core"], [])
        test = self.load_and_validate("test")
        self.assertEqual(len([m for m in test if m.origin == "user"]), 1)


class SampleInputProfile(unittest.TestCase):
    """The repository's sample modules (modules/examples) through the test-profile outputs. Read only:
    the outputs are built in memory and compared with the committed files by --check."""

    def setUp(self):
        gen.ENGINE_API = gen.parse_engine_api()
        gen.FONT_CHARS = gen.parse_font_chars()
        self.release = gen.load_modules("release")
        self.test = gen.load_modules("test")
        self.outputs = gen.build_outputs(self.release, self.test)

    def output(self, directory, name):
        return self.outputs[os.path.join(directory, name)]

    def test_sample_input_is_only_in_the_test_profile(self):
        release_ids = [m.id for m in self.release]
        test_inputs = [m for m in self.test if m.kind == "input"]
        self.assertNotIn("user.example.pad_velocity", release_ids)
        self.assertEqual(len(test_inputs), 7)
        self.assertEqual(test_inputs[-1].id, "user.example.pad_velocity")
        self.assertEqual(test_inputs[-1].scene_key, "ex_padvel")

    def test_test_profile_tables_list_the_sample_input(self):
        registry = self.output(gen.TEST_GEN_DIR, "input_registry_examples.cpp")
        self.assertIn("user_example_pad_velocity_input_descriptor", registry)
        self.assertIn("kGeneratedInputCount", registry)
        keys = self.output(gen.TEST_GEN_DIR, "registry_examples.cpp")
        self.assertIn('{"user.example.pad_velocity", "ex_padvel"}', keys)
        layout = self.output(gen.TEST_GEN_DIR, "scene_layout_examples.hpp")
        self.assertIn('{"ex_padvel", 4, 12, 170}', layout)
        self.assertIn('{"ex_padvel", "pad_vel", AttributeType::Byte, 12}', layout)
        counts = self.output(gen.TEST_GEN_DIR, "module_counts.hpp")
        self.assertIn("kGeneratedInputCount = 7;", counts)
        union = self.output(gen.TEST_GEN_DIR, "input_state_examples.hpp")
        self.assertIn("PadVelocityInputState user_example_pad_velocity;", union)

    def test_release_outputs_do_not_name_the_sample(self):
        for name in ("registry_generated.cpp", "input_registry_generated.cpp", "scene_layout_generated.hpp"):
            text = self.output(gen.GEN_DIR, name)
            self.assertNotIn("ex_padvel", text, name)
            self.assertNotIn("pad_velocity", text, name)

    def test_firmware_headers_switch_only_under_the_example_macro(self):
        for name in ("input_state_generated.hpp", "scene_layout_generated.hpp", "module_counts_generated.hpp"):
            text = self.output(gen.GEN_DIR, name)
            self.assertIn("#ifdef ORBIT_NATIVE_EXAMPLE_PROFILE", text, name)
            self.assertIn("#else", text, name)
            self.assertTrue(text.rstrip().endswith("#endif"), name)

    def test_wrapper_keeps_the_release_body_in_the_else_branch(self):
        body = "#pragma once\nnamespace orbit { int x; }\n"
        wrapped = gen.wrap_example("// marker\n" + body, os.path.join(gen.TEST_GEN_DIR, "x.hpp"))
        self.assertEqual(wrapped.split("#else\n")[1].split("#endif")[0], body)
        self.assertIn('#include "../../../../../tests/native/generated/x.hpp"', wrapped)


if __name__ == "__main__":
    unittest.main()
