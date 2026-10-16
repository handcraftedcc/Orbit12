"""Generate t09i golden traces by running the Python Quantize and Strum sources under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09i/gen_traces.py

Writes tests/native/fixtures/t09i_source_traces.inc. Each trace becomes a C++ function that
replays the same steps through the C++ descriptor; expected note-ons/offs are the outputs
recorded from circuitpython-source/src/apps/Orion/modules/{quantize,strum}.py.

Time: strum uses adafruit_ticks milliseconds (2**29 wrap, via shims/adafruit_ticks.py). Each
step carries an unwrapped millisecond value U. Python sees U mod 2**29; C++ sees
(U + 2**32 - 2**29) mod 2**32, which wraps at the same U.

Transport ticks: quantize uses the unbounded Python midi_tick. Each step carries an absolute
tick A (it may exceed 2**32); C++ receives A mod 2**32 and its unwrapped tick reproduces A.

Float parameters: the source stores tilt as a Python double. C++ stores the float32 value
(ParamValue carries float), so the generator rounds every tilt to float32 before applying it to
the source. Both sides then compute with the same double, and the fixtures are bit-identical.

Traces named dev_* carry documented deviations (AGENTS.md release safety): the C++ result
is given explicitly and must differ from the source result.
"""
import os
import struct
import sys
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09i_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.quantize import Quantize  # noqa: E402
from apps.Orion.modules.strum import Strum  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_TICK_MOD = 1 << 32
CPP_OFFSET = (1 << 32) - (1 << 29)

# C++ ParamSpec order (main parms only).
QUANTIZE_PARM_INDEX = {"rate": 0}
STRUM_PARM_INDEX = {"amount": 0, "order": 1, "tilt": 2, "tilt_note_count": 3, "articulate": 4}
FLOAT_PARMS = {"tilt"}


def f32(value):
    """Round to the nearest float32, as the C++ ParamSpec stores it."""
    return struct.unpack("f", struct.pack("f", float(value)))[0]


class Transport:
    def __init__(self):
        self.now = 0
        self.midi_tick = 0
        self.running = 0


class Helper:
    def __init__(self, transport, key, scale):
        self.transport = transport
        self.state = SimpleNamespace(key=key, scale=scale)
        self.ui_manager = None
        self.orion = None


def cpp_now(u):
    return (u + CPP_OFFSET) % (1 << 32)


def P(u, tick, running, ons=(), offs=()):
    return ("p", u, tick, running, list(ons), list(offs))


def S(name, value):
    return ("set", name, value)


def G(key, scale):
    return ("global", key, scale)


def apply_set(mod, name, value):
    parm = mod.get_parm_by_name(name)
    parm.set_value(value)
    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)


def cpp_set(index_map, name, value):
    index = index_map[name]
    if name in FLOAT_PARMS:
        return "  r.set_float(%d, %sf);" % (index, repr(float(value)))
    return "  r.set_int(%d, %d);" % (index, value)


def fmt_pairs(pairs):
    return ", ".join("{%d, %d}" % (p, v) for p, v in pairs)


def fmt_pitches(pitches):
    return ", ".join(str(p) for p in pitches)


def make_module(kind, transport, key, scale):
    helper = Helper(transport, key, scale)
    if kind == "quantize":
        return Quantize(helper, 2), QUANTIZE_PARM_INDEX
    return Strum(helper, 2), STRUM_PARM_INDEX


def run_trace(kind, setup, steps, deviations=None, tick_shift=0):
    """Run one trace through the Python source; return (C++ lines, python log)."""
    deviations = deviations or {}
    key, scale = 0, 1
    for item in setup:
        if item[0] == "global":
            key, scale = item[1], item[2]
    transport = Transport()
    mod, index_map = make_module(kind, transport, key, scale)
    lines = ["  r.set_global(%d, %d);" % (key, scale)]
    for item in setup:
        if item[0] == "set":
            value = f32(item[2]) if item[1] in FLOAT_PARMS else item[2]
            apply_set(mod, item[1], value)
            lines.append(cpp_set(index_map, item[1], value))
    log = []
    for index, step in enumerate(steps):
        kind_tag = step[0]
        if kind_tag == "p":
            _, u, tick, running, ons, offs = step
            transport.now = u % TICKS_PERIOD
            transport.midi_tick = tick
            transport.running = 1 if running else 0
            on_arr = NoteOnArray()
            for pitch, vel in ons:
                assert on_arr.append_value(pitch, velocity=vel)
            off_arr = NoteOffArray()
            for pitch in offs:
                assert off_arr.append_value(pitch)
            out_on, out_off = mod.process(on_arr, off_arr)
            eon = [(out_on.notes[i], out_on.velocities[i]) for i in range(out_on.length)]
            eoff = [out_off.notes[i] for i in range(out_off.length)]
            assert len(eon) <= 12 and len(eoff) <= 12, "source batch overflow is not a fixture"
            head = "%du, %du, %s, {%s}, {%s}" % (
                cpp_now(u), (tick + tick_shift) % CPP_TICK_MOD, "true" if running else "false",
                fmt_pairs(ons), fmt_pitches(offs))
            if index in deviations:
                cpp_on, cpp_off = deviations[index]
                assert [tuple(x) for x in cpp_on] != eon or cpp_off != eoff, "deviation must differ"
                lines.append("  r.step_dev(%s, {%s}, {%s}, {%s}, {%s});" % (
                    head, fmt_pairs(eon), fmt_pitches(eoff), fmt_pairs(cpp_on), fmt_pitches(cpp_off)))
                log.append(("dev", index))
                continue
            lines.append("  r.step(%s, {%s}, {%s});" % (head, fmt_pairs(eon), fmt_pitches(eoff)))
            log.append(("p", u, tick, eon, eoff))
        elif kind_tag == "stop":
            mod.stop()
            lines.append("  r.stop();")
            log.append(("stop",))
        elif kind_tag == "set":
            value = f32(step[2]) if step[1] in FLOAT_PARMS else step[2]
            apply_set(mod, step[1], value)
            lines.append(cpp_set(index_map, step[1], value))
            log.append(("set", step[1], value))
        else:
            raise ValueError(step)
    return lines, log


# ---------------------------------------------------------------- trace builders

def ticks(start, stop, *, unit=10, ons=None, offs=None, running=True, base=0):
    """One process call per tick; ons/offs keyed by tick. Time advances `unit` ms per tick."""
    ons = ons or {}
    offs = offs or {}
    return [P(base + t * unit, t, running, ons.get(t, []), offs.get(t, [])) for t in range(start, stop)]


CHORD3 = [(60, 100), (64, 90), (67, 80)]
CHORD5 = [(60, 100), (64, 90), (67, 80), (72, 70), (76, 60)]
ORDER_CHORD = [(67, 100), (60, 90), (72, 80), (64, 70), (62, 60)]


def quantize_rate(index):
    return [S("rate", index)]


def build_quantize():
    t = {}
    t["defaults"] = ([], ticks(0, 40, ons={3: CHORD3}, offs={20: [60], 27: [64]}))
    # Every rate table entry, with on/off at non-aligned ticks.
    for rate in range(13):
        t["rate_%02d" % rate] = (quantize_rate(rate),
                                 ticks(0, 120, ons={5: [(60, 100)], 17: [(64, 90)], 41: [(60, 70)]},
                                       offs={23: [60], 44: [64], 70: [60]}))
    # Note-on exactly on a grid tick waits a full rate period; one tick earlier waits one tick.
    t["grid_exact"] = (quantize_rate(7), ticks(0, 24, ons={8: [(60, 100)], 15: [(64, 100)]}, offs={20: [60]}))
    t["grid_before"] = (quantize_rate(7), ticks(0, 24, ons={7: [(60, 100)]}, offs={9: [60]}))
    # Off timing follows the note-on delay: off_tick = off_tick_now + delay + 1.
    t["off_delay_chain"] = (quantize_rate(12), ticks(0, 16, ons={0: [(60, 100)]}, offs={0: [60], 3: []}))
    t["off_before_promotion"] = (quantize_rate(6), ticks(0, 20, ons={2: [(60, 100)]}, offs={3: [60]}))
    t["same_batch_on_off"] = (quantize_rate(5), ticks(0, 24, ons={4: [(60, 100)]}, offs={4: [60]}))
    t["retrigger_held"] = (quantize_rate(5), ticks(0, 30, ons={1: [(60, 100)], 10: [(60, 80)], 20: [(60, 60)]},
                                                  offs={14: [60]}))
    t["chord_mixed_delays"] = (quantize_rate(6), ticks(0, 40, ons={2: CHORD3, 9: [(62, 90)]},
                                                      offs={5: [64], 16: [60, 67], 22: [62]}))
    t["release_skip_on_batch"] = (quantize_rate(12), ticks(0, 12, ons={0: [(60, 100)], 4: [(60, 100)]},
                                                           offs={2: [60]}))
    # Transport restarts (tick moves backwards) keep the absolute grid, as the source does.
    t["tick_backwards"] = (quantize_rate(7), (ticks(0, 14, ons={3: [(60, 100)]}, offs={11: [60]})
                                              + [P(200 + t * 10, 5 + t, True) for t in range(0, 10)]
                                              + [P(300, 2, True), P(310, 3, True), P(320, 4, True),
                                                 P(330, 5, True)]))
    # Wrap at 2**32. The source stores scheduled times in array('I'), so absolute ticks at or above
    # 2**32 raise OverflowError there (no oracle). The trace is shifted down by 2**31, which is a
    # multiple of rates 1, 2, 4, 8, 16 and 32, so the grid is identical. C++ receives the
    # unshifted value mod 2**32: the crossing is at the same grid step as in the shifted trace.
    base = 0xFFFFFFF0 - (1 << 31)
    # Deviation Q3: the tick restarts backwards while a note-on is pending. The source releases the
    # note at tick 10 (not yet promoted, so nothing is sent) and then promotes it at tick 24: it sounds
    # with no release (stuck). C++ releases it at tick 25, after promotion.
    t["dev_backwards_pending_release"] = (quantize_rate(7), [
        P(20, 20, True, [(60, 100)], []), P(5, 5, True, [], [60]), P(10, 10, True),
        P(24, 24, True), P(25, 25, True), P(26, 26, True)])
    t["wrap_32_rate_8"] = (quantize_rate(7), [P(base + k, base + k, True, [(60, 100)] if k == 3 else [],
                                                [60] if k == 25 else []) for k in range(0, 40)])
    t["wrap_32_rate_16"] = (quantize_rate(5), [P(base + k, base + k, True, [(64, 100)] if k == 9 else [],
                                                 [64] if k == 40 else []) for k in range(0, 60)])
    # Stop clears pending, scheduled and held state; later offs for cleared notes emit nothing.
    t["stop_clears"] = (quantize_rate(7), ticks(0, 6, ons={0: CHORD3}, offs={3: [64]})
                        + [("stop",)] + ticks(6, 20, offs={9: [60], 12: [67]}))
    # Fill the pending array (12) and refuse nothing; a 13th note-on evicts the oldest pending.
    t["pending_full_evicts"] = (quantize_rate(12), [P(k * 10, k, True,
                                                              [(40 + k, 100)] if k < 12 else [(60, 90)] if k == 12 else [],
                                                              []) for k in range(0, 14)])
    # Scheduled offs at capacity: the oldest release is emitted early, as the source does.
    t["scheduled_full"] = (quantize_rate(0), [P(k * 10, k, True,
                                                       [(40 + k, 100)] if k == 0 else [],
                                                       []) for k in range(0, 3)]
                           + [P(30, 3, True, [(40 + i, 100) for i in range(1, 12)], [])]
                           + [P(40 + k * 10, 4 + k, True, [], [40 + k]) for k in range(0, 12)])
    # Held array at capacity: the 13th promotion emits an off for the oldest held note.
    t["held_full"] = (quantize_rate(12), [P(k * 10, k, True, [(40 + k, 100)] if k < 13 else [], [])
                                          for k in range(0, 14)])
    return t


def build_strum():
    t = {}
    # Source defaults (AMT=30, ORDR=ORD, ARTC=EVEN). EVEN resolves to the BNC pattern (see report).
    t["defaults"] = ([], ticks(0, 60, ons={0: CHORD5}, offs={50: [60, 64, 67, 72, 76]}))
    t["order_up"] = ([S("order", 1)], ticks(0, 60, ons={0: ORDER_CHORD}, offs={50: [60, 62, 64, 67, 72]}))
    t["order_down"] = ([S("order", 2)], ticks(0, 60, ons={0: ORDER_CHORD}, offs={50: [60, 62, 64, 67, 72]}))
    t["order_random"] = ([S("order", 4)], ticks(0, 60, ons={0: ORDER_CHORD}, offs={50: [60, 62, 64, 67, 72]}))
    t["order_ord"] = ([S("order", 0)], ticks(0, 60, ons={0: ORDER_CHORD}, offs={50: [60, 62, 64, 67, 72]}))
    # UPDWN reverses on even strum counts and keeps ascending on odd ones.
    t["order_updn_two_strums"] = ([S("order", 3)],
                                  ticks(0, 90, ons={0: CHORD5, 45: CHORD5}, offs={40: [60, 64, 67, 72, 76],
                                                                                  85: [60, 64, 67, 72, 76]}))
    t["order_down_two_strums"] = ([S("order", 2)],
                                  ticks(0, 90, ons={0: CHORD3, 45: CHORD3}, offs={40: [60, 64, 67],
                                                                                 85: [60, 64, 67]}))
    t["amount_zero"] = ([S("amount", 0)], ticks(0, 20, ons={0: CHORD5}, offs={15: [60, 64, 67, 72, 76]}))
    t["amount_large"] = ([S("amount", 250)], ticks(0, 80, ons={0: CHORD3}, offs={70: [60, 64, 67]}, unit=10))
    for art in range(9):
        t["articulate_%d" % art] = ([S("articulate", art), S("amount", 120)],
                                    ticks(0, 60, ons={0: CHORD5}, offs={55: [60, 64, 67, 72, 76]}))
    # Tilt: float32 values; negative tilt raises the early notes less, positive tilt lowers them.
    t["tilt_positive"] = ([S("tilt", 0.3), S("order", 0)],
                          ticks(0, 60, ons={0: [(60, 127), (64, 127), (67, 127), (72, 127), (76, 127)]},
                                offs={55: [60, 64, 67, 72, 76]}))
    t["tilt_negative"] = ([S("tilt", -0.4), S("order", 0)],
                          ticks(0, 60, ons={0: [(60, 100), (64, 100), (67, 100), (72, 100), (76, 100)]},
                                offs={55: [60, 64, 67, 72, 76]}))
    t["tilt_full"] = ([S("tilt", 1.0), S("amount", 60)],
                      ticks(0, 40, ons={0: [(60, 90), (64, 90), (67, 90), (72, 90), (76, 90)]},
                            offs={35: [60, 64, 67, 72, 76]}))
    # CNT is stored but the source passes STRUM_CHORD_LENGTH (5) to the tilt, so it has no effect.
    t["cnt_inert"] = ([S("tilt", 0.5), S("tilt_note_count", 1)],
                      ticks(0, 50, ons={0: CHORD5}, offs={45: [60, 64, 67, 72, 76]}))
    # Off before the strummed note: the register drops it and nothing sounds.
    t["off_before_emit"] = ([S("amount", 100)],
                            ticks(0, 30, ons={0: CHORD5}, offs={2: [76, 72], 5: [67]}))
    # Stop clears the register but not the strum schedule (source keeps strum_active/count).
    t["stop_mid_strum"] = ([S("amount", 60)], ticks(0, 10, ons={0: CHORD5})
                           + [("stop",)] + ticks(10, 30, ons={20: [(62, 100), (65, 100)]},
                                                 offs={28: [62, 65]}))
    # A register at capacity keeps the first 12 pitches; a 13th pitch is dropped silently.
    t["register_full"] = ([S("amount", 0)], ticks(0, 14, ons={0: [(40 + i, 100) for i in range(12)],
                                                              1: [(60, 90)]},
                                                  offs={20: [40]}))
    # Time wrap at the 2**29 millisecond period, with strum deadlines crossing the boundary.
    wrap = TICKS_PERIOD - 35
    t["wrap_2_29"] = ([S("amount", 40)],
                      [P(wrap + k * 10, k, True, CHORD3 if k == 0 else [], []) for k in range(0, 4)]
                      + [P(wrap + 30 + k * 10, 4 + k, True) for k in range(0, 12)]
                      + [P(wrap + 150, 16, True, [], [60, 64, 67])])
    return t


# Traces named dev_* carry a documented deviation (see the cpp file header for Q/S numbering).
DEVIATIONS = {
    # Step 4 (tick 25): the source sends nothing; C++ releases the note promoted at tick 24.
    "dev_backwards_pending_release": {4: ([], [60])},
}


def main():
    traces = [("quantize", n, s) for n, s in build_quantize().items()]
    traces += [("strum", n, s) for n, s in build_strum().items()]
    chunks = [
        "// Generated by tests/native/fixtures/t09i/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source Quantize or Strum trace.",
        "// Source results were produced by python -I; no generated trace carries a deviation.",
        "",
    ]
    summary = []
    names = {"quantize": [], "strum": []}
    for kind, name, (setup, steps) in sorted(traces, key=lambda item: (item[0], item[1])):
        names[kind].append(name)
        shift = (1 << 31) if name.startswith("wrap_32") else 0
        lines, log = run_trace(kind, setup, steps, DEVIATIONS.get(name, {}), shift)
        chunks.append("void trace_%s_%s(Run& r) {" % (kind, name))
        chunks.extend(lines)
        chunks.append("}")
        chunks.append("")
        summary.append((kind + "/" + name, len(steps)))
    for kind in ("quantize", "strum"):
        chunks.append("const NamedTrace k%sTraces[] = {" % kind.capitalize())
        for name in names[kind]:
            chunks.append('    {"%s", trace_%s_%s},' % (name, kind, name))
        chunks.append("};")
        chunks.append("")
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name, count in summary:
        print("%-36s steps=%d" % (name, count))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
