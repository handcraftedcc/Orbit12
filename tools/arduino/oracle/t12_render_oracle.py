# T12 render oracle: runs the real CircuitPython apps/Orion/core/ui.py under
# CPython with displayio/vectorio fakes that rasterize the group tree into a
# 128x64 monochrome canvas, then packs it in SH1106 page order (byte
# x + (y/8)*128, bit y%8, bit 0 = top row) exactly as Adafruit_SH110X.display()
# transmits it. Expected checksums and UiModel builders in
# tests/native/fixtures/t12_render_traces.inc come from this script, never from
# the C++ renderer under test.
#
# Usage: python -I t12_render_oracle.py <repo_root> <output.inc> [--ascii <case>]
#
# Fakes implement only what ui.py uses: Group (hidden/x/y/scale), TileGrid
# (tile indices, opaque or transparent palette index 0 via make_transparent),
# vectorio.Rectangle (palette index 0 color), and OnDiskBitmap decoding the
# 1-bit UI_Design BMPs. Scale multiplies child offsets and sizes, as displayio.
# The phraser entry is hidden from the module registry (AGENTS.md), so the
# registry stub drops it before the selector index is taken.
import os
import struct
import sys
import types

REPO = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2])
ASCII_CASE = sys.argv[sys.argv.index("--ascii") + 1] if "--ascii" in sys.argv else None
SRC = os.path.join(REPO, "circuitpython-source", "src")
UI_DIR = os.path.join(REPO, "circuitpython-source", "UI_Design")
sys.path.insert(0, SRC)

# ---------------------------------------------------------------- displayio fakes


class Palette(list):
    def __init__(self, count):
        super().__init__([0] * count)
        self.transparent = set()

    def make_transparent(self, index):
        self.transparent.add(index)


class BmpSheet:
    """1-bit BMP decoded to rows of bits (top row first)."""

    def __init__(self, path):
        with open(path, "rb") as handle:
            data = handle.read()
        assert data[:2] == b"BM", path
        offset = struct.unpack_from("<I", data, 10)[0]
        width, height = struct.unpack_from("<ii", data, 18)
        assert struct.unpack_from("<H", data, 28)[0] == 1, "1 bpp only"
        self.width = width
        self.height = abs(height)
        stride = ((width + 31) // 32) * 4
        self.rows = []
        for y in range(self.height):
            source = y if height < 0 else self.height - 1 - y
            base = offset + source * stride
            self.rows.append([(data[base + x // 8] >> (7 - x % 8)) & 1 for x in range(width)])

    def bit(self, x, y):
        return self.rows[y][x]


class OnDiskBitmap:
    def __init__(self, path):
        name = os.path.basename(path)
        self.sheet = BmpSheet(os.path.join(UI_DIR, name))


class Group(list):
    def __init__(self):
        super().__init__()
        self.hidden = False
        self.x = 0
        self.y = 0
        self.scale = 1


class TileGrid:
    def __init__(self, bitmap, pixel_shader, width, height, tile_width, tile_height, x=0, y=0):
        self.sheet = bitmap.sheet
        self.palette = pixel_shader
        self.width = width
        self.height = height
        self.tile_width = tile_width
        self.tile_height = tile_height
        self.x = x
        self.y = y
        self.tiles = [0] * (width * height)

    def __getitem__(self, index):
        return self.tiles[index]

    def __setitem__(self, index, value):
        self.tiles[index] = value


class Rectangle:
    def __init__(self, pixel_shader, width, height, x=0, y=0):
        self.pixel_shader = pixel_shader
        self.width = width
        self.height = height
        self.x = x
        self.y = y


ticks = types.ModuleType("adafruit_ticks")
ticks.ticks_ms = lambda: 0
ticks.ticks_add = lambda t, d: (t + d) % (1 << 29)
ticks.ticks_diff = lambda a, b: ((a - b + (1 << 28)) % (1 << 29)) - (1 << 28)
sys.modules["adafruit_ticks"] = ticks

displayio = types.ModuleType("displayio")
displayio.Group = Group
displayio.TileGrid = TileGrid
displayio.Palette = Palette
displayio.OnDiskBitmap = OnDiskBitmap
vectorio = types.ModuleType("vectorio")
vectorio.Rectangle = Rectangle
sys.modules["displayio"] = displayio
sys.modules["vectorio"] = vectorio

import apps.Orion.core.state as state_module  # noqa: E402  (after fakes)
import apps.Orion.core.ui as ui  # noqa: E402
import apps.Orion.core.parms as Parms  # noqa: E402
from apps.Orion.core.music import NOTES, SCALENAMES  # noqa: E402

PROCESSOR_LABELS = [("empty", "EMPTY"), ("arp", "ARP"), ("arp_walk", "ARPWLK"), ("bounce", "BOUNCE"),
                    ("chord", "CHRD"), ("chopper", "CHOPPR"), ("delay", "DELAY"), ("drop", "DROP"),
                    ("euclid", "EUCLID"), ("gate", "GATE"), ("latch", "LATCH"), ("pick", "PICK"),
                    ("quantize", "QNT"), ("randomize", "RND"), ("retrigger", "RTRGR"), ("strum", "STRUM"),
                    ("transpose", "TRNS"), ("wander", "WANDR")]
INPUT_LABELS = [("note", "NOTE"), ("chords", "CHRD"), ("drum", "DRUM"), ("matrix", "MTRX"),
                ("note_walk", "NOTEWLK"), ("chord_walk", "CHRDWLK")]
# ui.py reads the registries only for the module selector; the stubs keep the
# same labels as the real registries minus the hidden phraser entry.
ui.ModuleRegistry = types.SimpleNamespace(AVAILABLE_MODULE_LABELS=[label for _, label in PROCESSOR_LABELS])
ui.InputModuleRegistry = types.SimpleNamespace(AVAILABLE_MODULE_LABELS=[label for _, label in INPUT_LABELS])

# ---------------------------------------------------------------- rasterizer


class Canvas:
    def __init__(self):
        self.px = [[0] * 128 for _ in range(64)]

    def put(self, x, y, on):
        if 0 <= x < 128 and 0 <= y < 64:
            self.px[y][x] = on

    def page_bytes(self):
        frame = bytearray(1024)
        for y in range(64):
            for x in range(128):
                if self.px[y][x]:
                    frame[x + (y >> 3) * 128] |= 1 << (y & 7)
        return bytes(frame)

    def ascii(self):
        rows = []
        for y in range(64):
            rows.append("".join("#" if self.px[y][x] else "." for x in range(128)))
        return "\n".join(rows)


def render_node(canvas, node, origin, scale):
    """origin/scale: parent coordinate origin (absolute px) and px-per-unit."""
    if isinstance(node, Group):
        if node.hidden:
            return
        child_origin = (origin[0] + scale * node.x, origin[1] + scale * node.y)
        child_scale = scale * node.scale
        for child in node:
            render_node(canvas, child, child_origin, child_scale)
    elif isinstance(node, Rectangle):
        ox = origin[0] + scale * node.x
        oy = origin[1] + scale * node.y
        on = 1 if node.pixel_shader[0] != 0 else 0
        for py in range(node.height * scale):
            for px in range(node.width * scale):
                canvas.put(ox + px, oy + py, on)
    elif isinstance(node, TileGrid):
        ox = origin[0] + scale * node.x
        oy = origin[1] + scale * node.y
        columns = node.sheet.width // node.tile_width
        palette = node.palette
        for j in range(node.height):
            for i in range(node.width):
                tile = node.tiles[j * node.width + i]
                tcol = tile % columns
                trow = tile // columns
                assert trow * node.tile_height < node.sheet.height, "tile index out of sheet"
                for ty in range(node.tile_height):
                    for tx in range(node.tile_width):
                        bit = node.sheet.bit(tcol * node.tile_width + tx, trow * node.tile_height + ty)
                        if bit in palette.transparent:
                            continue
                        on = 1 if palette[bit] != 0 else 0
                        for sy in range(scale):
                            for sx in range(scale):
                                canvas.put(ox + scale * (i * node.tile_width + tx) + sx,
                                           oy + scale * (j * node.tile_height + ty) + sy, on)
    else:
        raise TypeError(type(node))


# ---------------------------------------------------------------- fake state


class FakeParm:
    def __init__(self, label, value):
        self.label = label
        self.display_value = str(value)


class FakeModule:
    def __init__(self, name, label, parms=()):
        self.name = name
        self.label = label
        self.label_length = len(label)
        # (label, value) tuples are plain Integer rows; Parm objects are the real
        # circuitpython parms classes (typed rows: Boolean, Enum, Decimal, ...).
        self._parms = [FakeParm(*p) if isinstance(p, tuple) else p for p in parms]

    def get_parms(self):
        return self._parms


class FakeTransport:
    def __init__(self, running):
        self.running = running


class FakeDisplay:
    def __init__(self):
        self.root_group = None


class FakeMacropad:
    def __init__(self):
        self.display = FakeDisplay()


class FakeState:
    pass


STANDARD_CHAIN = [
    FakeModule("set", "SET"),
    FakeModule("note", "NOTE"),
    FakeModule("transpose", "TRNS"),
    FakeModule("empty", "EMPTY"),
    FakeModule("drop", "DROP"),
    FakeModule("empty", "EMPTY"),
    FakeModule("euclid", "EUCLID"),
    FakeModule("empty", "EMPTY"),
    FakeModule("output", "OUT"),
    FakeModule("settings", "SETTINGS"),
]

SIX_PARMS = [("RATE", 3), ("OCT", -2), ("GATE", 50), ("VEL", 127), ("PTN", 4), ("MUTE", 0)]

# One row of every typed Parm kind (real parms.py classes; values are two-decimal
# so float32 storage on the target keeps the Python display text).
TYPED_PARMS = [
    Parms.Parm("bool_p", "BOOL", Parms.BooleanParmType, 1),
    Parms.Parm("enum_p", "ENUM", Parms.EnumParmType, 2, options=["NEXT", "ADD", "OUT+N", "OUT+S", "SKIP"]),
    Parms.Parm("dec_p", "DEC", Parms.FloatParmType, 0.75),
    Parms.Parm("pct_p", "PCT", Parms.PercentParmType, 0.5),
    Parms.Parm("rate_p", "RATE", Parms.RateParmType, 4),
    Parms.Parm("ptn_p", "PTN", Parms.PatternParmType, 9),
    Parms.Parm("note_p", "NOTE", Parms.NoteParmType, 7),
    Parms.Parm("btn_p", "GO", Parms.ButtonParmType, 0),
]
# Decimal/percent edge values: negative fractions, int-typed float parm, sign of zero.
EDGE_PARMS = [
    Parms.Parm("dec_neg", "NEG", Parms.FloatParmType, -0.125),
    Parms.Parm("dec_int", "KEYBRI", Parms.FloatParmType, 1),
    Parms.Parm("dec_big", "BIG", Parms.FloatParmType, 1234.5),
    Parms.Parm("pct_big", "PCT", Parms.PercentParmType, 1.25),
    Parms.Parm("pct_zero", "PCTZ", Parms.PercentParmType, -0.004),
    Parms.Parm("bool_off", "OFF?", Parms.BooleanParmType, 0),
    Parms.Parm("rate_last", "LAST", Parms.RateParmType, 12),
    Parms.Parm("note_low", "LOW", Parms.NoteParmType, 0),
]


def scenario(name, **kw):
    base = dict(
        section=ui.UISection.CHAIN, chain_mode=state_module.ChainModes.SELECT, active_chain=1,
        active_parm=0, chain=None, selector=0, nav=0, transport_mode=0, running=False,
        key=0, scale=1, octave=3, scene=0, key_custom=None,
    )
    base.update(kw)
    base["name"] = name
    return base


SCENARIOS = [
    scenario("chain_select_in", active_chain=1),
    scenario("chain_select_set", active_chain=0),
    scenario("chain_swap_out", active_chain=8, chain_mode=state_module.ChainModes.SWAP),
    scenario("chain_swap_empty_slot", active_chain=3, chain_mode=state_module.ChainModes.SWAP),
    scenario("chain_settings_running", active_chain=9, running=True, transport_mode=1, nav=1, scene=9),
    scenario("parm_row0", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=0,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_row1", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=1,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_row2_scroll1", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=3,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_row3_scroll2", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=5,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_edit_row2", section=ui.UISection.PARMEDIT, active_chain=2, active_parm=2,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_module_row_len4", section=ui.UISection.PARMSELECTION, active_chain=4, active_parm=-1,
             chain=("drop", "GATE", SIX_PARMS[:3])),
    scenario("parm_module_row_len6", section=ui.UISection.PARMSELECTION, active_chain=4, active_parm=-1,
             chain=("drop", "ARPWLK", SIX_PARMS[:3])),
    scenario("parm_chain_marker", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=-2,
             chain=("transpose", "TRNS", SIX_PARMS)),
    scenario("parm_two_rows_blank", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=1,
             chain=("transpose", "TRNS", SIX_PARMS[:2])),
    scenario("parm_long_value_and_label", section=ui.UISection.PARMSELECTION, active_chain=2,
             active_parm=0, chain=("transpose", "TRNS", [("LONGLABEL", 123456), ("NEG", -1234), ("A", 7),
                                                          ("B", 8)])),
    scenario("selector_input", section=ui.UISection.MODULESELECTION, active_chain=1, selector=2),
    scenario("selector_processor", section=ui.UISection.MODULESELECTION, active_chain=3, selector=5),
    scenario("header_negative_octave_long_scale", key=3, scale=3, octave=-2),
    scenario("header_drum_custom", key_custom="DRUM", active_chain=1),
    scenario("header_stopped_scene3", active_chain=5, scene=2, nav=0, transport_mode=0, running=False),
    scenario("typed_row0_bool", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=0,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("typed_row3_percent", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=3,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("typed_row5_pattern_scroll", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=5,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("typed_row7_button_scroll", section=ui.UISection.PARMSELECTION, active_chain=2, active_parm=7,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("typed_edit_enum", section=ui.UISection.PARMEDIT, active_chain=2, active_parm=1,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("typed_edit_note", section=ui.UISection.PARMEDIT, active_chain=2, active_parm=6,
             chain=("transpose", "TRNS", TYPED_PARMS)),
    scenario("edge_values_row1", section=ui.UISection.PARMSELECTION, active_chain=4, active_parm=1,
             chain=("drop", "DROP", EDGE_PARMS)),
    scenario("edge_values_row5_scroll", section=ui.UISection.PARMSELECTION, active_chain=4, active_parm=5,
             chain=("drop", "DROP", EDGE_PARMS)),
]

# ---------------------------------------------------------------- build a screen


def build_ui(spec):
    st = FakeState()
    st.active_ui_section = spec["section"]
    st.active_chain = spec["active_chain"]
    st.active_chain_mode = spec["chain_mode"]
    st.active_parm = spec["active_parm"]
    st.module_selector_active_module = spec["selector"]
    st.nav_keys_state = spec["nav"]
    st.transport_mode = spec["transport_mode"]
    st.active_scene = spec["scene"]
    st.key = spec["key"]
    st.scale = spec["scale"]
    st.octave = spec["octave"]
    st.key_custom_text = spec["key_custom"]
    chain = [FakeModule(m.name, m.label) for m in STANDARD_CHAIN]
    if spec["chain"] is not None:
        name, label, parms = spec["chain"]
        chain[spec["active_chain"]] = FakeModule(name, label, parms)
    st.chain_modules = chain
    transport = FakeTransport(spec["running"])
    macropad = FakeMacropad()
    manager = ui.UIManager(macropad, st, transport, defer_refresh=True)
    hf = manager.header_footer
    hf.update_header_key_info()
    hf.update_header_chain_id()
    hf.update_header_module_label()
    hf.update_footer_state_icons()
    hf.update_header_chain_preview()
    hf.update_footer_scene_info()
    manager.chain.set_cursor(st.active_chain)
    manager.parameter_section.rebuild_parm_section()
    manager.module_selector.update_module_name()
    manager.switch_section()
    canvas = Canvas()
    render_node(canvas, manager.main_group, (0, 0), 1)
    return st, manager, canvas


def fnv1a32(data):
    value = 0x811C9DC5
    for byte in data:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def float32(value):
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def typed_row_lines(parm, row, ident, prelude):
    """UiRow fields for a real parms.py Parm. Values are the Python source values;
    Decimal/Percent use float32 storage, which is exact for two-decimal values."""
    kind_type = parm.type
    out = []

    def put(field, text):
        out.append("  m.rows[%d].%s = %s;" % (row, field, text))

    if kind_type is Parms.BooleanParmType:
        put("kind", "orbit::RowKind::Boolean")
        put("ivalue", str(int(parm.value)))
    elif kind_type is Parms.EnumParmType:
        table = "kOptions_%s_%d" % (ident, row)
        prelude.append("inline constexpr const char* const %s[%d] = {%s};" % (
            table, len(parm.options), ", ".join(cpp_string(option) for option in parm.options)))
        put("kind", "orbit::RowKind::Enum")
        put("ivalue", str(int(parm.value)))
        put("options", table)
        put("option_count", str(len(parm.options)))
    elif kind_type is Parms.FloatParmType:
        put("kind", "orbit::RowKind::Decimal")
        put("fvalue", repr(float32(parm.value)) + "f")
        if isinstance(parm.value, float):
            put("float_text", "true")  # Python float: str() keeps ".0" and two decimals
    elif kind_type is Parms.PercentParmType:
        put("kind", "orbit::RowKind::Percent")
        put("fvalue", repr(float32(parm.value)) + "f")
    elif kind_type is Parms.PatternParmType:
        put("kind", "orbit::RowKind::Pattern")
        put("ivalue", str(int(parm.value)))
    elif kind_type is Parms.RateParmType:
        assert parm.include_rates and not parm.include_bars, "only rates-only rate rows are modelled"
        put("kind", "orbit::RowKind::Rate")
        put("ivalue", str(int(parm.value)))
    elif kind_type is Parms.NoteParmType:
        assert not parm.multiple_octaves, "only single-octave note rows are modelled"
        put("kind", "orbit::RowKind::Note")
        put("ivalue", str(int(parm.value)))
    elif kind_type is Parms.ButtonParmType:
        put("kind", "orbit::RowKind::Button")
    else:
        raise ValueError("no UiRow mapping for %r" % (kind_type,))
    return out


def emit_model(spec, st, manager):
    ps = manager.parameter_section
    lines = []
    prelude = []
    ident = spec["name"]
    lines.append("inline orbit::UiModel make_%s() {" % ident)
    lines.append("  orbit::UiModel m{};")
    section_names = ["Chain", "ParmSelection", "ParmEdit", "ModuleSelection"]
    lines.append("  m.section = orbit::UiSection::%s;" % section_names[int(spec["section"])])
    lines.append("  m.chain_mode = orbit::UiChainMode::%s;" % ("Select" if spec["chain_mode"] == 0 else "Swap"))
    lines.append("  m.active_chain = %d;" % spec["active_chain"])
    lines.append("  m.active_parm = %d;" % spec["active_parm"])
    # Same field as controller.cpp model(): raw active_parm - scroll_offset. The
    # renderer derives the layout row itself (clamped), as the Python layout does.
    highlight = st.active_parm - ps.scroll_offset
    lines.append("  m.parm_count = %d;" % ps.parm_count)
    lines.append("  m.scroll_offset = %d;" % ps.scroll_offset)
    lines.append("  m.highlight_row = %d;" % highlight)
    module = st.chain_modules[st.active_chain]
    if ps.parm_count:
        parms = module.get_parms()
        for row in range(4):
            index = ps.scroll_offset + row
            if index < ps.parm_count:
                parm = parms[index]
                lines.append("  m.rows[%d].label = %s;" % (row, cpp_string(parm.label)))
                if isinstance(parm, Parms.Parm):
                    lines.extend(typed_row_lines(parm, row, ident, prelude))
                else:
                    lines.append("  m.rows[%d].kind = orbit::RowKind::Integer;" % row)
                    lines.append("  m.rows[%d].ivalue = %s;" % (row, parm.display_value))
                lines.append("  m.rows[%d].present = true;" % row)
    for index, slot in enumerate(st.chain_modules):
        lines.append("  m.slots[%d].label = %s;" % (index, cpp_string(slot.label)))
        lines.append("  m.slots[%d].empty = %s;" % (index, "true" if slot.name == "empty" else "false"))
    lines.append("  m.selector_index = %d;" % st.module_selector_active_module)
    selector_label = (ui.InputModuleRegistry if st.active_chain == 1
                      else ui.ModuleRegistry).AVAILABLE_MODULE_LABELS[st.module_selector_active_module]
    lines.append("  m.selector_label = %s;" % cpp_string(selector_label))
    lines.append("  m.nav_keys_state = %d;" % st.nav_keys_state)
    lines.append("  m.transport_running = %s;" % ("true" if spec["running"] else "false"))
    lines.append("  m.transport_mode = orbit::TransportMode::%s;" %
                 ("External" if st.transport_mode == 1 else "Internal"))
    lines.append("  m.key = %d;" % st.key)
    lines.append("  m.scale = %d;" % st.scale)
    lines.append("  m.octave = %d;" % st.octave)
    if st.key_custom_text:
        lines.append("  m.key_custom_text = %s;" % cpp_string(st.key_custom_text))
    lines.append("  m.active_scene = %d;" % st.active_scene)
    lines.append("  return m;")
    lines.append("}")
    return "\n".join(prelude + lines)


def main():
    cases = []
    chunks = []
    for spec in SCENARIOS:
        st, manager, canvas = build_ui(spec)
        frame = canvas.page_bytes()
        checksum = fnv1a32(frame)
        if ASCII_CASE == spec["name"]:
            print(canvas.ascii())
        chunks.append(emit_model(spec, st, manager))
        cases.append('{"%s", 0x%08XU, &make_%s}' % (spec["name"], checksum, spec["name"]))
    text = [
        "// Generated by tools/arduino/oracle/t12_render_oracle.py from the real",
        "// circuitpython-source/src/apps/Orion/core/ui.py under CPython (rasterized",
        "// displayio fakes, SH1106 page packing). Do not edit by hand.",
        "// Included by tests/native/t12_renderer.cpp after GoldenCase is declared.",
        "",
        "namespace t12_fixture {",
    ]
    text.extend(chunks)
    text.append("")
    text.append("inline constexpr GoldenCase kCases[] = {")
    for case in cases:
        text.append("    " + case + ",")
    text.append("};")
    text.append("}  // namespace t12_fixture")
    text.append("")
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(text))
    print("wrote %d cases to %s" % (len(cases), OUT))


if __name__ == "__main__":
    main()
