"""Generate t09e golden traces by running the Python Arp source under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09e/gen_traces.py

Writes tests/native/fixtures/t09e_source_traces.inc. Each trace becomes a C++ function
that replays the same steps through the C++ descriptor; expected note-ons/offs are the
outputs recorded from circuitpython-source/src/apps/Orion/modules/arp.py.

Parameters are set the way scenes.py loads them: Parm.set_value, then the edit callback.
Time: each step carries an unwrapped millisecond value U. Python sees U mod 2**29
(adafruit_ticks wrap, via shims/adafruit_ticks.py). C++ sees (U + 2**32 - 2**29) mod 2**32,
which wraps at the same U, so every difference seen by the source is reproduced exactly.

Trace functions named trace_dev_* carry documented deviations (AGENTS.md release safety):
a step's C++ result is given explicitly and must differ from the source result.
"""
import os
import sys
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09e_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.arp import Arp  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_OFFSET = (1 << 32) - (1 << 29)

# C++ ParamSpec order (main parms only).
PARM_INDEX = {
    "rate": 0, "sort_mode": 1, "play_mode": 2, "transpose_steps": 3, "max_transpose": 4,
    "boundary_mode": 5, "gate": 6, "pattern": 7, "pattern_shift": 8,
    "random_pattern_length": 9, "rand_seed": 10, "retrigger_mode": 11,
}
FLOAT_PARMS = {"gate"}


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


def apply_set(mod, name, value):
    parm = mod.get_parm_by_name(name)
    parm.set_value(value)
    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)


def cpp_set(name, value):
    index = PARM_INDEX[name]
    if name in FLOAT_PARMS:
        return "  r.set_float(%d, %sf);" % (index, repr(float(value)))
    return "  r.set_int(%d, %d);" % (index, value)


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
    mod = Arp(Helper(transport, SimpleNamespace(key=key, scale=scale)), 2)
    lines = ["  r.set_global(%d, %d);" % (key, scale)]
    for item in setup:
        if item[0] == "set":
            apply_set(mod, item[1], item[2])
            lines.append(cpp_set(item[1], item[2]))
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
            eon = [(out_on.notes[i], out_on.velocities[i]) for i in range(out_on.length)]
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
            lines.append(cpp_set(step[1], step[2]))
            log.append(("set", step[1], step[2]))
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


CHORD4 = [(60, 100), (64, 90), (67, 80), (72, 70)]
CHORD5 = [(60, 100), (64, 90), (67, 80), (72, 70), (76, 60)]
CHORD3 = [(60, 100), (64, 90), (67, 80)]
PLAY_RATE = [S("rate", 12), S("gate", 50)]


def build_traces():
    t = {}
    t["defaults"] = ([], ticks(0, 60, ons={0: CHORD3}, offs={40: [60], 48: [64]}))
    t["sort_down"] = ([S("sort_mode", 1), S("gate", 50)], ticks(0, 40, ons={0: CHORD4}, offs={30: [72]}))
    t["sort_ord"] = ([S("sort_mode", 2), S("gate", 50)],
                     ticks(0, 40, ons={0: [(67, 100), (60, 90), (72, 80), (64, 70)], 12: [(62, 60)]}))
    t["sort_change_no_resort"] = ([S("gate", 50)], ticks(0, 60, ons={0: CHORD4},
                                                        offs={}, step=1)
                                  + [S("sort_mode", 1)] + ticks(60, 90, ons={}))
    t["sort_change_on_note"] = ([S("gate", 50)], ticks(0, 30, ons={0: CHORD3, 15: [(62, 100)]}))
    for mode in range(9):
        # Repeat emits all five holds per step. A 10 ms gate lets the source's generate-before-release
        # order overflow its 12-entry schedule (see dev_schedule_full); a zero gate does not.
        t["play_%d_five" % mode] = ([S("play_mode", mode)] + PLAY_RATE + ([S("gate", 0)] if mode == 8 else []),
                                    ticks(0, 40, ons={0: CHORD5}))
    for mode in (1, 2, 3, 4, 5, 6):
        t["play_%d_four" % mode] = ([S("play_mode", mode)] + PLAY_RATE, ticks(0, 40, ons={0: CHORD4}))
    t["random_len0"] = ([S("play_mode", 7), S("rand_seed", 0)] + PLAY_RATE, ticks(0, 40, ons={0: CHORD5}))
    t["random_len5_seed"] = ([S("play_mode", 7), S("random_pattern_length", 5), S("rand_seed", 1234)] + PLAY_RATE,
                             ticks(0, 40, ons={0: CHORD5}))
    t["random_sort_down"] = ([S("play_mode", 7), S("sort_mode", 1), S("rand_seed", 77)] + PLAY_RATE,
                             ticks(0, 30, ons={0: CHORD4}))
    t["repeat_chords"] = ([S("play_mode", 8), S("rate", 12), S("gate", 0)],
                          ticks(0, 40, ons={0: [(60, 100), (64, 90), (67, 80)], 20: [(62, 70)]},
                                offs={28: [60]}))
    for boundary in range(5):
        t["transpose_bound_%d" % boundary] = (
            [G(2, 2), S("transpose_steps", -3), S("max_transpose", 4), S("boundary_mode", boundary)] + PLAY_RATE,
            ticks(0, 30, ons={0: CHORD4}))
    t["transpose_unbounded_sort"] = ([G(2, 2), S("transpose_steps", 5)] + PLAY_RATE,
                                     ticks(0, 30, ons={0: [(60, 100), (64, 90), (67, 80)]}))
    t["transpose_unbounded_repeat"] = ([G(0, 1), S("transpose_steps", -7), S("play_mode", 8), S("gate", 30)] + [S("rate", 12)],
                                       ticks(0, 24, ons={0: [(60, 100), (64, 90)]}))
    t["pattern_negative_shift"] = ([S("pattern", 17), S("pattern_shift", -3)] + PLAY_RATE,
                                   ticks(0, 40, ons={0: CHORD4}))
    t["pattern_positive_shift"] = ([S("pattern", 13), S("pattern_shift", 5)] + PLAY_RATE,
                                   ticks(0, 40, ons={0: CHORD4}))
    t["pattern_rests_default"] = ([S("pattern", 20), S("gate", 50), S("rate", 12)],
                                  ticks(0, 40, ons={0: CHORD3}))
    t["stable_idle"] = ([S("retrigger_mode", 2), S("pattern", 5)] + PLAY_RATE, ticks(
        0, 60, ons={0: CHORD3}, offs={24: [60, 64, 67]}, step=1)
        + ticks(60, 70, ons={}))
    t["stable_repeat"] = ([S("retrigger_mode", 2), S("play_mode", 8), S("pattern", 5), S("rate", 12), S("gate", 30)],
                          ticks(0, 30, ons={0: [(60, 100), (64, 90)]}))
    t["stable_rate6_release"] = ([S("retrigger_mode", 2), S("pattern", 9), S("rate", 8), S("gate", 50)],
                                 ticks(0, 60, ons={0: CHORD3, 30: [(62, 60)]}, offs={20: [60]}))
    t["cont_transport_restart"] = ([S("retrigger_mode", 1), S("play_mode", 3)] + PLAY_RATE, (
        ticks(0, 15, ons={0: CHORD3}, offs={8: [60, 64, 67]})
        + ticks(15, 20, ons={15: []}, running=True)
        + ticks(20, 24, running=False)
        + ticks(24, 40, ons={24: [(62, 100)]}, running=True)))
    t["rtrg_release_restart"] = ([S("retrigger_mode", 0), S("play_mode", 2)] + PLAY_RATE, (
        ticks(0, 15, ons={0: CHORD3}, offs={8: [60, 64, 67]})
        + ticks(15, 20)
        + ticks(20, 30, ons={20: [(62, 100)]})))
    t["rtrg_same_frame"] = ([S("retrigger_mode", 0), S("play_mode", 1)] + PLAY_RATE,
                            ticks(0, 30, ons={0: [(60, 100), (64, 90)]}, offs={10: [60, 64]})
                            + ticks(30, 36, ons={}))
    t["duplicate_hold"] = ([S("gate", 50), S("play_mode", 0)] + [S("rate", 12)], ticks(
        0, 14, ons={0: [(60, 100)], 2: [(60, 80)]}, offs={4: [60], 6: [60]}))
    t["mid_bin_press"] = ([S("gate", 50)], ticks(0, 30, ons={4: [(60, 100)], 7: [(64, 100)]}))
    t["tick_backwards"] = ([S("gate", 50), S("rate", 12)], [
        P(t * 10, t, True, [(60, 100)] if t == 0 else [], []) for t in range(0, 6)]
        + [P(30, 3, True), P(40, 4, True), P(50, 5, True), P(60, 6, True), P(70, 7, True)])
    t["rate_change_jump"] = ([S("gate", 5), S("rate", 8), S("play_mode", 0)], (
        ticks(0, 101, ons={0: [(60, 100), (64, 90), (67, 80)]})
        + ticks(101, 106)  # let the pending release from tick 102 leave before the jump
        + [S("rate", 12), P(9600, 960, True)]))  # 944 triggers; the source stops at 12 emissions
    t["gate_zero"] = ([S("gate", 0), S("rate", 12)], ticks(0, 14, ons={0: [(60, 100)]}))
    t["gate_fractional"] = ([S("gate", 37.9), S("rate", 12)], ticks(0, 14, ons={0: [(60, 100)]}))
    t["capacity_12_held"] = ([S("gate", 50), S("rate", 12)], ticks(
        0, 14, ons={0: [(60 + i, 90) for i in range(12)], 1: [(80, 100)]}, offs={5: [60]}))
    t["stop_pending"] = ([S("gate", 500), S("rate", 12)], (
        ticks(0, 6, ons={0: CHORD3}) + [("stop",)] + ticks(6, 14)))
    t["wrap_2_29"] = ([S("gate", 100), S("rate", 12), S("play_mode", 1)], [
        P((TICKS_PERIOD - 60) + t * 10, t, True, [(60, 100)] if t == 0 else [], []) for t in range(0, 14)]
        + [P(TICKS_PERIOD - 60 + 150 + 10 * k, 14 + k, True) for k in range(0, 4)])
    # Deviation traces: a full schedule (12 pending releases) refuses further emissions.
    t["dev_schedule_full"] = ([S("gate", 500), S("rate", 12)],
                              ticks(0, 13, ons={0: [(60 + i, 100) for i in range(12)]}))
    t["dev_repeat_full"] = ([S("gate", 500), S("rate", 12), S("play_mode", 8)],
                            ticks(0, 3, ons={0: [(60 + i, 100) for i in range(12)]}))
    return t


# index is the step index in the trace; cpp_on/cpp_off are the documented C++ results.
DEVIATIONS = {
    # Step 12 would emit note 72 (12th pending) and drop its release; C++ refuses both.
    "dev_schedule_full": {12: ([], [])},
    # Step 1 emits 12 notes with dropped releases; C++ refuses the whole emission.
    "dev_repeat_full": {1: ([], []), 2: ([], [])},
}


def main():
    traces = build_traces()
    chunks = [
        "// Generated by tests/native/fixtures/t09e/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source Arp trace.",
        "// Functions named trace_dev_* carry documented deviations: step_dev() checks the C++",
        "// result and asserts the source (python -I) result differs as recorded here.",
        "",
    ]
    summary = []
    for name in sorted(traces):
        setup, steps = traces[name]
        lines, log = run_trace(setup, steps, DEVIATIONS.get(name, {}))
        prefix = "trace_"  # Deviation traces are already named dev_*.
        chunks.append("void %s%s(Run& r) {" % (prefix, name))
        chunks.extend(lines)
        chunks.append("}")
        chunks.append("")
        summary.append((name, len(steps)))
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name, count in summary:
        print("%-30s steps=%d" % (name, count))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
