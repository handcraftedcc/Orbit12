"""Generate t09f golden traces by running the Python ArpWalk source under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09f/gen_traces.py

Writes tests/native/fixtures/t09f_source_traces.inc. Each trace becomes a C++ function that
replays the same steps through the C++ descriptor; expected note-ons/offs are recorded from
circuitpython-source/src/apps/Orion/modules/arp_walk.py (with core/note_array.py, core/utils.py,
core/music.py and core/parms.py).

Parameters are set the way scenes.py loads them: Parm.set_value, then the edit callback.
Time: each step carries an unwrapped millisecond value U. Python sees U mod 2**29
(adafruit_ticks wrap, via shims/adafruit_ticks.py). C++ sees (U + 2**32 - 2**29) mod 2**32.

Trace functions named trace_dev_* carry documented deviations (AGENTS.md release safety):
a step's C++ result is given explicitly and must differ from the source result.
"""
import os
import sys
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09f_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.arp_walk import ArpWalk  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_OFFSET = (1 << 32) - (1 << 29)

# C++ ParamSpec order: the Python get_parms() main order, including the action button.
PARM_INDEX = {
    "rate": 0, "sort_mode": 1, "step_length": 2, "step_select": 3, "step_value": 4,
    "randomize_steps": 5, "invert": 6, "max_walk": 7, "boundary_mode": 8, "gate": 9,
    "rhythm_pattern": 10, "pattern_shift": 11, "retrigger_mode": 12,
}
FLOAT_PARMS = {"gate"}
BOOL_PARMS = {"invert"}


class Transport:
    def __init__(self):
        self.now = 0
        self.midi_tick = 0
        self.running = 0


class Helper:
    def __init__(self, transport, state):
        self.transport = transport
        self.state = state
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


def R():
    return ("enter",)


def apply_set(mod, name, value):
    parm = mod.get_parm_by_name(name)
    parm.set_value(value)
    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)


def cpp_value(name, value):
    if name in FLOAT_PARMS:
        return "  r.set_float(%d, %sf);" % (PARM_INDEX[name], repr(float(value)))
    if name in BOOL_PARMS:
        return "  r.set_bool(%d, %s);" % (PARM_INDEX[name], "true" if value else "false")
    return "  r.set_int(%d, %d);" % (PARM_INDEX[name], value)


def fmt_pairs(pairs):
    return ", ".join("{%d, %d}" % (p, v) for p, v in pairs)


def fmt_pitches(pitches):
    return ", ".join(str(p) for p in pitches)


def run_trace(setup, steps, deviations=None):
    """Run one trace through the Python source; return (C++ lines, python log)."""
    deviations = deviations or {}
    key, scale = 0, 1
    for item in setup:
        if item[0] == "global":
            key, scale = item[1], item[2]
    transport = Transport()
    mod = ArpWalk(Helper(transport, SimpleNamespace(key=key, scale=scale)), 2)
    lines = ["  r.set_global(%d, %d);" % (key, scale)]
    for item in setup:
        if item[0] == "set":
            apply_set(mod, item[1], item[2])
            lines.append(cpp_value(item[1], item[2]))
    log = []
    for index, step in enumerate(steps):
        kind = step[0]
        if kind == "p":
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
            eon = [(out_on.notes[i], out_on.velocity_at(i)) for i in range(out_on.length)]
            eoff = [out_off.notes[i] for i in range(out_off.length)]
            head = "%du, %d, %s, {%s}, {%s}" % (
                cpp_now(u), tick, "true" if running else "false", fmt_pairs(ons), fmt_pitches(offs))
            if index in deviations:
                cpp_on, cpp_off = deviations[index]
                assert [tuple(x) for x in cpp_on] != eon or cpp_off != eoff, "deviation must differ"
                lines.append("  r.step_dev(%s, {%s}, {%s}, {%s}, {%s});" % (
                    head, fmt_pairs(eon), fmt_pitches(eoff), fmt_pairs(cpp_on), fmt_pitches(cpp_off)))
                log.append(("dev", index))
                continue
            lines.append("  r.step(%s, {%s}, {%s});" % (head, fmt_pairs(eon), fmt_pitches(eoff)))
            log.append(("p", u, tick, eon, eoff))
        elif kind == "stop":
            mod.stop()
            lines.append("  r.stop();")
            log.append(("stop",))
        elif kind == "set":
            apply_set(mod, step[1], step[2])
            lines.append(cpp_value(step[1], step[2]))
            log.append(("set", step[1], step[2]))
        elif kind == "enter":
            mod.get_parm_by_name("randomize_steps").enter()
            lines.append("  r.press_randomize();")
            log.append(("enter",))
        else:
            raise ValueError(step)
    return lines, log


# ---------------------------------------------------------------- trace builders

def ticks(start, stop, *, unit=10, ons=None, offs=None, running=True, step=1):
    """One process call per tick; ons/offs keyed by tick. Time advances `unit` ms per tick."""
    ons = ons or {}
    offs = offs or {}
    out = []
    for t in range(start, stop, step):
        out.append(P(t * unit, t, running, ons.get(t, []), offs.get(t, [])))
    return out


CHORD3 = [(60, 100), (64, 90), (67, 80)]
CHORD4 = [(60, 100), (64, 90), (67, 80), (72, 70)]
RATE12 = [S("rate", 12), S("gate", 50)]


def steps_set(values):
    """step_length then each step_select/step_value pair (scene-load order)."""
    out = [S("step_length", len(values))]
    for index, value in enumerate(values):
        out.append(S("step_select", index))
        out.append(S("step_value", value))
    return out


def build_traces():
    t = {}
    # Defaults: LEN 2 [-1, 2], single note at default rate 8 (6 ticks).
    t["defaults_single"] = ([], ticks(0, 60, ons={0: [(60, 100)]}, offs={40: [60]}))
    t["defaults_chord"] = ([S("gate", 50)], ticks(0, 40, ons={0: CHORD3}, offs={36: [60, 64]}))
    t["single_walk_rate12"] = (RATE12, ticks(0, 40, ons={0: [(60, 100)]}))
    # All eight saved steps, normal and inverted.
    eight = [3, -2, 5, 0, -4, 1, 2, -1]
    t["steps_all8"] = (steps_set(eight) + RATE12, ticks(0, 40, ons={0: [(60, 100)]}))
    t["steps_all8_invert"] = (steps_set(eight) + [S("invert", True)] + RATE12,
                              ticks(0, 40, ons={0: [(60, 100)]}))
    t["steps_len1"] = (steps_set([5]) + RATE12, ticks(0, 20, ons={0: [(60, 100)]}))
    t["steps_len8_chord"] = (steps_set(eight) + RATE12, ticks(0, 40, ons={0: CHORD4}))
    # Length shrink and selection clamp (Parm.set_value clamps to the current minmax).
    t["steps_shrink_clamp"] = (steps_set(eight) + RATE12, ticks(0, 12, ons={0: [(60, 100)]})
                               + [S("step_length", 3), S("step_select", 6), S("step_value", 30)]
                               + ticks(12, 30))
    t["steps_clamp_low"] = ([S("step_length", 20), S("step_select", -3), S("step_value", -40), S("gate", 50),
                             S("rate", 12)], ticks(0, 20, ons={0: [(60, 100)]}))
    # Walker boundaries and max walk on single and chord registers.
    for boundary in range(5):
        t["bound_single_%d" % boundary] = (
            steps_set([2, 3, -1]) + [S("max_walk", 4), S("boundary_mode", boundary)] + RATE12,
            ticks(0, 30, ons={0: [(60, 100)]}))
        t["bound_chord_%d" % boundary] = (
            steps_set([4, -3]) + [S("max_walk", 3), S("boundary_mode", boundary)] + RATE12,
            ticks(0, 30, ons={0: CHORD3}))
    # Chord indexes into the register with octave carry; high register forces corrections.
    t["chord_octave_correction"] = (steps_set([5, 5, 5]) + RATE12,
                                    ticks(0, 36, ons={0: [(100, 100), (110, 90), (120, 80)]}))
    t["chord_low_correction"] = (steps_set([-7]) + RATE12,
                                 ticks(0, 30, ons={0: [(2, 100), (9, 90), (14, 80)]}))
    # Clamp boundary: the walk saturates, and chord corrections run past the clamp limit.
    t["clamp_chord_high"] = (steps_set([5, 5]) + [S("max_walk", 3), S("boundary_mode", 4)] + RATE12,
                             ticks(0, 40, ons={0: [(120, 100), (124, 90), (127, 80)]}))
    t["clamp_chord_low_invert"] = (steps_set([-6, 7]) + [S("max_walk", 2), S("boundary_mode", 4),
                                                         S("invert", True)] + RATE12,
                                   ticks(0, 40, ons={0: [(2, 100), (9, 90), (14, 80)]}))
    # Walk drifts far above the clamp limit while notes stay in range, then a high register forces
    # a correction from that far walk back into range (the clamp skip keeps this O(1)).
    t["clamp_drift_then_high"] = (steps_set([24]) + [S("max_walk", 3), S("boundary_mode", 4),
                                                     S("retrigger_mode", 1)] + RATE12,
                                  ticks(0, 302, ons={0: [(0, 100), (1, 100)], 301: [(120, 100), (124, 100), (127, 100)]},
                                        offs={301: [0, 1]}))
    # Sort modes; ORD keeps press order.
    t["sort_down_chord"] = (steps_set([1, 2]) + [S("sort_mode", 1)] + RATE12, ticks(0, 30, ons={0: CHORD4}))
    t["sort_ord_chord"] = (steps_set([1, 2]) + [S("sort_mode", 2)] + RATE12,
                           ticks(0, 30, ons={0: [(67, 100), (60, 90), (72, 80), (64, 70)]}))
    t["sort_change_on_note"] = (steps_set([1, 2]) + RATE12,
                                ticks(0, 30, ons={0: CHORD3, 14: [(62, 100)]}))
    # Pattern (RYTM) and shift.
    t["pattern_negative_shift"] = (steps_set([1, 2]) + [S("rhythm_pattern", 17), S("pattern_shift", -3)] + RATE12,
                                   ticks(0, 40, ons={0: CHORD3}))
    t["pattern_positive_shift"] = (steps_set([1, 2]) + [S("rhythm_pattern", 13), S("pattern_shift", 5)] + RATE12,
                                   ticks(0, 40, ons={0: CHORD3}))
    t["pattern_rests"] = (steps_set([1, 2]) + [S("rhythm_pattern", 20)] + RATE12,
                          ticks(0, 40, ons={0: CHORD3}))
    t["pattern_last"] = (steps_set([3, -1]) + [S("rhythm_pattern", 24)] + RATE12,
                         ticks(0, 40, ons={0: [(60, 100)]}))
    # Retrigger modes.
    t["rtrg_same_frame"] = (steps_set([1, 2]) + [S("retrigger_mode", 0)] + RATE12,
                            ticks(0, 30, ons={0: CHORD3}, offs={10: [60, 64, 67]}) + ticks(30, 34))
    t["cont_transport_restart"] = (steps_set([1, 2]) + [S("retrigger_mode", 1)] + RATE12, (
        ticks(0, 15, ons={0: CHORD3}, offs={8: [60, 64, 67]})
        + ticks(15, 20, running=True)
        + ticks(20, 24, running=False)
        + ticks(24, 40, ons={24: [(62, 100)]}, running=True)))
    t["stbl_idle"] = (steps_set([2, 2, 2]) + [S("retrigger_mode", 2), S("rhythm_pattern", 5)] + RATE12,
                      ticks(0, 40, ons={0: CHORD3}, offs={24: [60, 64, 67]}))
    t["rtrg_restart_after_release"] = (steps_set([2, -1]) + [S("retrigger_mode", 0)] + RATE12, (
        ticks(0, 15, ons={0: CHORD3}, offs={8: [60, 64, 67]})
        + ticks(15, 20)
        + ticks(20, 30, ons={20: [(62, 100)]})))
    # Duplicate holds and mid-bin presses.
    t["duplicate_hold"] = (steps_set([1]) + RATE12, ticks(0, 14, ons={0: [(60, 100)], 2: [(60, 80)]},
                                                           offs={4: [60], 6: [60]}))
    t["mid_bin_press"] = (steps_set([1]) + [S("gate", 50)], ticks(0, 30, ons={4: [(60, 100)], 7: [(64, 100)]}))
    # Randomize: the seed is random_counter + transport tick; presses come after a processed pass.
    t["randomize_twice"] = (steps_set([0, 0, 0, 0]) + RATE12, (
        ticks(0, 6, ons={0: [(60, 100)]})
        + [R()] + ticks(6, 12) + [R()] + ticks(12, 24)))
    t["randomize_len8_chord"] = (steps_set([0] * 8) + RATE12, (
        ticks(0, 4, ons={0: CHORD3}) + [R()] + ticks(4, 40)))
    t["randomize_then_invert"] = (steps_set([0, 0, 0]) + RATE12, (
        ticks(0, 3, ons={0: [(60, 100)]}) + [R(), S("invert", True)] + ticks(3, 30)))
    # Stop: releases are dropped; positions reset; random_counter survives.
    t["stop_pending"] = (steps_set([1, 2]) + [S("gate", 500), S("rate", 12)], (
        ticks(0, 6, ons={0: CHORD3}) + [("stop",)] + ticks(6, 14)))
    t["stop_then_restart"] = (steps_set([3, -2]) + RATE12, (
        ticks(0, 8, ons={0: [(60, 100)]}) + [("stop",)] + ticks(8, 16, ons={8: [(64, 100)]})))
    t["tick_backwards"] = (steps_set([1, 2]) + [S("gate", 50), S("rate", 12)], [
        P(t * 10, t, True, [(60, 100)] if t == 0 else [], []) for t in range(0, 6)]
        + [P(30, 3, True), P(40, 4, True), P(50, 5, True), P(60, 6, True), P(70, 7, True)])
    t["wrap_2_29"] = (steps_set([1, 2]) + RATE12, [
        P((TICKS_PERIOD - 60) + t * 10, t, True, [(60, 100)] if t == 0 else [], []) for t in range(0, 14)]
        + [P(TICKS_PERIOD - 60 + 150 + 10 * k, 14 + k, True) for k in range(0, 4)])
    # Large grid jump: 944 triggers in one pass, single note (walk tail is exact for single notes).
    t["jump_single_tail"] = (steps_set([2, -1, 3]) + [S("rate", 8), S("gate", 5)], (
        ticks(0, 101, ons={0: [(60, 100)]})
        + ticks(101, 106)
        + [S("rate", 12), P(9600, 960, True)]
        + ticks(961, 980)))
    t["gate_zero"] = (steps_set([1, 2]) + [S("gate", 0), S("rate", 12)], ticks(0, 14, ons={0: CHORD3}))
    t["gate_fractional"] = (steps_set([1, 2]) + [S("gate", 37.9), S("rate", 12)], ticks(0, 14, ons={0: CHORD3}))
    # Deviation: 12 pending releases fill the schedule; the source sends the note-on and drops its off.
    t["dev_schedule_full"] = (steps_set([1]) + [S("gate", 500), S("rate", 12)], (
        ticks(0, 13, ons={0: [(60 + i, 100) for i in range(12)]})))
    return t


DEVIATIONS = {
    # Step 12 (13th trigger) would emit a note whose release is dropped; C++ refuses both.
    "dev_schedule_full": {12: ([], [])},
    # Tick 961: the source generates 127 while the 12 releases from tick 960 still occupy the
    # schedule (pops happen after generation), so the 127 release is dropped and the note sticks.
    # C++ pops first, reuses the slot, and releases 127 at tick 962 (step 109).
    "jump_single_tail": {109: ([(127, 100)], [127])},
}


def main():
    traces = build_traces()
    chunks = [
        "// Generated by tests/native/fixtures/t09f/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source ArpWalk trace.",
        "// Deviation traces (dev_*) use step_dev(): the C++ result is checked and must differ from the source.",
        "",
    ]
    summary = []
    for name in sorted(traces):
        setup, steps = traces[name]
        lines, log = run_trace(setup, steps, DEVIATIONS.get(name, {}))
        chunks.append("void trace_%s(Run& r) {" % name)
        chunks.extend(lines)
        chunks.append("}")
        chunks.append("")
        summary.append((name, len(steps), log))
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name, count, log in summary:
        emitted = sum(1 for item in log if item[0] == "p" and item[3])
        print("%-30s steps=%d note_steps=%d" % (name, count, emitted))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
