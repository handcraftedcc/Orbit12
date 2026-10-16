"""Generate t09d golden traces by running the Python source under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09d/gen_traces.py

Writes tests/native/fixtures/t09d_source_traces.inc. Each trace becomes a C++
function that replays the same steps through the C++ descriptors; expected
note-ons/offs are the outputs recorded from circuitpython-source/src/apps/Orion.

Time: each step carries an unwrapped millisecond value U. Python sees
U mod 2**29 (adafruit_ticks wrap, via shims/adafruit_ticks.py). C++ sees
(U + 2**32 - 2**29) mod 2**32, which wraps at the same U, so every difference
seen by the source is reproduced exactly.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09d_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.gate import Gate  # noqa: E402
from apps.Orion.modules.chopper import Chopper  # noqa: E402
from apps.Orion.modules.retrigger import Retrigger  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_OFFSET = (1 << 32) - (1 << 29)
CLASSES = {"gate": Gate, "chopper": Chopper, "retrigger": Retrigger}


class Transport:
    def __init__(self):
        self.now = 0
        self.midi_tick = 0
        self.running = 0


class Helper:
    def __init__(self, transport):
        self.transport = transport
        self.state = None
        self.ui_manager = None
        self.orion = None


def cpp_now(u):
    return (u + CPP_OFFSET) % (1 << 32)


def P(u, tick, running, ons=(), offs=()):
    return ("p", u, tick, running, list(ons), list(offs))


def S(attr, value, index):
    return ("set", (attr, value, index))


def apply_set(mod, setter):
    attr, value, _index = setter
    if attr == "rate":
        mod.set_rate(value)
    else:
        setattr(mod, attr, value)


def cpp_set(setter):
    attr, value, index = setter
    if isinstance(value, float):
        return "  r.set_float(%d, %sf);" % (index, repr(value))
    return "  r.set_int(%d, %d);" % (index, value)


def fmt_notes(pairs):
    return ", ".join("{%d, %d}" % (p, v) for p, v in pairs)


def fmt_pitches(pitches):
    return ", ".join(str(p) for p in pitches)


def run_trace(kind, setup, steps, deviations=None):
    """Run one trace through the Python source; return (C++ lines, python log).

    deviations maps a step index to (cpp_eon, cpp_eoff) overrides. An override
    must differ from the source result; the step is then emitted as step_dev().
    """
    deviations = deviations or {}
    transport = Transport()
    mod = CLASSES[kind](Helper(transport), 2)
    lines = []
    log = []
    for setter in setup:
        apply_set(mod, setter)
        lines.append(cpp_set(setter))
    for index, step in enumerate(steps):
        if step[0] == "p":
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
                cpp_now(u), tick, "true" if running else "false", fmt_notes(ons), fmt_pitches(offs))
            if index in deviations:
                cpp_on, cpp_off = deviations[index]
                cpp_on = cpp_on or []
                cpp_off = eoff if cpp_off is None else cpp_off
                assert [tuple(x) for x in cpp_on] != eon or cpp_off != eoff, "deviation must differ"
                lines.append(
                    "  r.step_dev(%s, {%s}, {%s}, {%s}, {%s});"
                    % (head, fmt_notes(eon), fmt_pitches(eoff), fmt_notes(cpp_on), fmt_pitches(cpp_off)))
                log.append(("dev", u, tick, running, ons, offs, eon, eoff, cpp_on, cpp_off))
                continue
            lines.append("  r.step(%s, {%s}, {%s});" % (head, fmt_notes(eon), fmt_pitches(eoff)))
            log.append(("p", u, tick, running, ons, offs, eon, eoff))
        elif step[0] == "stop":
            mod.stop()
            lines.append("  r.stop();")
            log.append(("stop",))
        elif step[0] == "set":
            apply_set(mod, step[1])
            lines.append(cpp_set(step[1]))
            log.append(("set", step[1]))
        else:
            raise ValueError(step)
    return lines, log


# Parameter indices must match the C++ ParamSpec order.
GATE_SETUP = lambda gate=100, gate_random=0, pattern_length=0: [  # noqa: E731
    ("gate", gate, 0), ("gate_random", gate_random, 1), ("pattern_length", pattern_length, 2)]
CHOP_SETUP = lambda rate=8, chop=0.5, pattern_length=16, seed=0, rtrg=0: [  # noqa: E731
    ("rate", rate, 0), ("chop", chop, 1), ("pattern_length", pattern_length, 2),
    ("random_seed_user", seed, 3), ("retrigger_mode", rtrg, 4)]
RETR_SETUP = lambda mode=0, threshold=50: [("mode", mode, 0), ("threshold", threshold, 1)]  # noqa: E731


def gate_traces():
    traces = {}
    traces["gate_basic"] = ("gate", GATE_SETUP(), [
        P(0, 0, True, [(60, 100)]),
        P(50, 1, True, [], [60]),  # input off is not forwarded; the scheduled off replaces it
        P(99, 2, True),
        P(100, 3, True),  # deadline equal to now: not yet due
        P(101, 4, True),
        P(150, 5, True),
    ])
    traces["gate_random"] = ("gate", GATE_SETUP(gate=100, gate_random=25), (
        [P(u, u, True, [(60 + (u // 30) % 5, 90 + u % 30)] if u % 30 == 0 else []) for u in range(0, 420, 3)]
        + [P(u, u, True) for u in range(420, 560, 7)]))
    traces["gate_pattern"] = ("gate", GATE_SETUP(gate=40, gate_random=10, pattern_length=3), [
        P(u, u, True, [(64, 100)] if u % 12 == 0 else [], [64] if u % 12 == 6 else []) for u in range(0, 120)])
    traces["gate_param_change"] = ("gate", GATE_SETUP(gate=100), [
        P(0, 0, True, [(60, 100)]),
        S("gate", 30, 0),
        P(10, 1, True, [(62, 100)]),
        P(50, 2, True),
        P(130, 3, True),
    ])
    traces["gate_stop"] = ("gate", GATE_SETUP(gate=100), [
        P(0, 0, True, [(60, 100), (62, 90)]),
        P(10, 1, True),
        ("stop",),
        P(200, 2, True),
        P(210, 3, True, [(64, 100)]),
        P(309, 4, True),
        P(310, 5, True),
    ])
    traces["gate_negative"] = ("gate", GATE_SETUP(gate=-5), [
        P(0, 0, True, [(60, 100)]), P(0, 0, True), P(1, 0, True)])
    traces["gate_capacity"] = ("gate", GATE_SETUP(gate=100), [
        P(0, 0, True, [(60 + i, 100) for i in range(12)]),
        P(1, 0, True, [(72, 100)]),
        P(50, 0, True),
        P(101, 0, True),
        P(200, 0, True),
    ])
    traces["gate_wrap"] = ("gate", GATE_SETUP(gate=100), [
        P(TICKS_PERIOD - 30, 0, True, [(60, 100)]),
        P(TICKS_PERIOD - 1, 0, True),
        P(TICKS_PERIOD + 69, 0, True),
        P(TICKS_PERIOD + 70, 0, True),
        P(TICKS_PERIOD + 71, 0, True),
    ])
    return traces


def chopper_traces():
    traces = {}
    traces["chopper_default"] = ("chopper", CHOP_SETUP(), [
        P(u, u, True, [(60, 90)] if u == 0 else [(64, 80)] if u == 30 else [], [60] if u == 70 else [])
        for u in range(0, 100)])
    traces["chopper_chop_zero_one"] = ("chopper", CHOP_SETUP(chop=0.0, pattern_length=8),
        [P(u, u * 6, True, [(60, 100), (67, 100)] if u == 0 else []) for u in range(0, 16)]
        + [S("chop", 1.0, 1)]
        + [P(u, u * 6, True) for u in range(16, 30)])
    traces["chopper_seeded"] = ("chopper", CHOP_SETUP(rate=4, chop=0.25, pattern_length=8, seed=1234, rtrg=0), [
        P(u, u * 3, True, [(60, 100), (64, 70)] if u == 0 else [], [60] if u == 40 else [])
        for u in range(0, 80)])
    traces["chopper_cont"] = ("chopper", CHOP_SETUP(rate=8, chop=0.4, pattern_length=5, seed=7, rtrg=1), [
        P(u, u * 6, True, [(60, 100)] if u == 2 else [], [60] if u == 120 else []) for u in range(0, 60)])
    traces["chopper_rtrg"] = ("chopper", CHOP_SETUP(rate=8, chop=0.5, pattern_length=6, seed=3, rtrg=2), [
        P(0, 0, True),
        P(5, 30, True, [(60, 100)]),
        P(6, 36, True),
        P(7, 42, True, [(62, 100)]),
        P(8, 48, True, [], [60]),
        P(9, 54, True, [], [62]),
        P(10, 60, True),
        P(11, 66, True, [(64, 90)]),
        P(12, 72, True),
        P(13, 78, True),
        P(14, 84, True),
        P(15, 90, True),
        P(16, 96, True),
        P(17, 102, True),
    ])
    traces["chopper_not_running"] = ("chopper", CHOP_SETUP(chop=1.0), [
        P(0, 0, False, [(60, 100)]),
        P(1, 6, False, [], [60]),
        P(2, 12, True, [(62, 100)]),
        P(3, 18, True),
        P(4, 24, True),
    ])
    traces["chopper_stop"] = ("chopper", CHOP_SETUP(chop=0.5, pattern_length=4, seed=11), (
        [P(u, u * 6, True, [(60, 100)] if u == 0 else []) for u in range(0, 8)]
        + [("stop",)]
        + [P(u, u * 6, True) for u in range(0, 6)]))
    traces["chopper_rate_change"] = ("chopper", CHOP_SETUP(rate=8, chop=0.0, pattern_length=4, seed=5), (
        [P(0, 0, True, [(60, 100)])]
        + [P(u, u * 6, True) for u in range(1, 4)]
        + [S("rate", 6, 0)]
        + [P(u, u * 6, True) for u in range(4, 12)]))
    traces["chopper_held_overflow"] = ("chopper", CHOP_SETUP(chop=0.0, pattern_length=2, seed=0), [
        P(0, 0, True, [(60 + i, 100) for i in range(12)]),
        P(1, 6, True, [(80, 100)]),
        P(2, 12, True),
        P(3, 18, True, [], [70]),
    ])
    traces["chopper_evict_same_batch"] = ("chopper", CHOP_SETUP(chop=1.0, pattern_length=2, seed=0), [
        P(0, 0, True, [(60 + i, 100) for i in range(12)]),
        S("chop", 0.0, 1),
        P(1, 6, True, [(72, 100)]),  # grid opens 60..71, then 72 evicts 60 in the same batch
        P(2, 12, True),
    ])
    traces["chopper_capacity"] = ("chopper", CHOP_SETUP(chop=1.0, pattern_length=2, seed=0), [
        P(0, 0, True, [(60 + i, 100) for i in range(11)]),
        S("chop", 0.0, 1),
        P(1, 6, True, [(80, 100), (81, 100)]),
        P(2, 12, True),
        P(3, 18, True),
    ])
    return traces


def retrigger_traces():
    traces = {}
    traces["retrigger_press_zero"] = ("retrigger", RETR_SETUP(mode=0, threshold=0), [
        P(0, 0, True, [(60, 100)]),
        P(10, 0, True, [(62, 100)]),
        P(20, 0, True, [], [60]),
        P(30, 0, True, [(64, 100)]),
    ])
    traces["retrigger_threshold_boundary"] = ("retrigger", RETR_SETUP(mode=0, threshold=50), [
        P(0, 0, True, [(60, 100)]),
        P(10, 0, True, [(62, 100)]),  # last event 0 -> due at 50; blocked
        P(60, 0, True, [(64, 100)]),  # last event 10 -> due exactly at 60: retrigger
        P(109, 0, True, [(65, 100)]),  # last event 60 -> due at 110: blocked
        P(110, 0, True, [(66, 100)]),  # due exactly at 110: retrigger
    ])
    traces["retrigger_release"] = ("retrigger", RETR_SETUP(mode=1, threshold=0), [
        P(0, 0, True, [(60, 100)]),
        P(5, 0, True, [(62, 100)]),
        P(10, 0, True, [], [60]),
        P(15, 0, True, [], [62]),
    ])
    traces["retrigger_both"] = ("retrigger", RETR_SETUP(mode=2, threshold=0), [
        P(0, 0, True, [(60, 100)]),
        P(5, 0, True, [(62, 100)]),
        P(10, 0, True, [], [60]),
        P(15, 0, True, [(64, 100)], [62]),
    ])
    traces["retrigger_full_release"] = ("retrigger", RETR_SETUP(mode=0, threshold=0), [
        P(0, 0, True, [(60, 100), (62, 100)]),
        P(5, 0, True, [], [60, 62]),
        P(6, 0, True, [(64, 90)]),
    ])
    traces["retrigger_stop"] = ("retrigger", RETR_SETUP(mode=1, threshold=100), [
        P(0, 0, True, [(60, 100)]),
        P(10, 0, True, [], [60]),
        P(20, 0, True, [(62, 100)]),
        ("stop",),
        P(26, 0, True, [(64, 100)]),
        P(27, 0, True, [], [62]),
    ])
    traces["retrigger_wrap"] = ("retrigger", RETR_SETUP(mode=0, threshold=100), [
        P(TICKS_PERIOD - 10, 0, True, [(60, 100)]),
        P(TICKS_PERIOD + 89, 0, True, [(62, 100)]),  # 99 after last event: blocked
        P(TICKS_PERIOD + 189, 0, True, [(64, 100)]),  # exactly 100 after last event: retrigger
        P(TICKS_PERIOD + 190, 0, True, [(65, 100)]),
    ])
    traces["retrigger_offs_overflow"] = ("retrigger", RETR_SETUP(mode=1, threshold=0), [
        P(0, 0, True, [(60 + i, 100) for i in range(12)]),
        P(1, 0, True, [], [99]),
    ])
    return traces


# Documented deviations (AGENTS.md release safety): where the source silently
# drops an on/off because a fixed NoteArray is full, the C++ result is given
# per step index. Each entry must differ from the source at exactly that step.
#   gate_capacity: 13th on is refused instead of passing with no scheduled off.
#   chopper_capacity: 81 is refused (not marked active); the next grid step
#     emits it instead of leaving an active note with no on sent.
#   retrigger_offs_overflow: the retrigger is skipped when its off batch would
#     exceed 12 entries, instead of silently dropping the last held off.
DEVIATIONS = {
    "gate_capacity": {1: ([], None)},
    # Step indices count the ("set", ...) entry: index 1 is the chop=0 setter, index 2 is tick 6.
    # 60 is evicted by 81 after its grid-open on (deviation 7): C++ cancels the pair.
    "chopper_capacity": {2: ([(p, 100) for p in range(61, 71)] + [(80, 100), (81, 100)], [])},
    # Source: 60 is released in the same batch as its grid-open on (stuck). C++ cancels both.
    "chopper_evict_same_batch": {2: ([(p, 100) for p in range(61, 73)], [])},
    "retrigger_offs_overflow": {1: ([], [99])},
}


def main():
    traces = {}
    traces.update(gate_traces())
    traces.update(chopper_traces())
    traces.update(retrigger_traces())
    chunks = [
        "// Generated by tests/native/fixtures/t09d/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source trace.",
        "// Functions named trace_dev_* carry documented deviations: step_dev() checks the",
        "// C++ result and asserts the source (python -I) result differs as recorded here.",
        "",
    ]
    summary = []
    for name in sorted(traces):
        kind, setup, steps = traces[name]
        lines, log = run_trace(kind, setup, steps, DEVIATIONS.get(name, {}))
        if name in DEVIATIONS:
            chunks.append("void trace_dev_%s(Run& r) {" % name)
        else:
            chunks.append("void trace_%s(Run& r) {" % name)
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
