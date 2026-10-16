"""Generate t09h golden traces by running the Python Latch and Pick sources under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09h/gen_traces.py

Writes tests/native/fixtures/t09h_source_traces.inc. Each trace becomes a C++ function that
replays the same steps through the C++ descriptor; expected note-ons/offs are the outputs
recorded from circuitpython-source/src/apps/Orion/modules/latch.py and pick.py.

Neither module reads time, so steps carry no clock. Source note arrays carry pitch and
velocity; channel 0 is used throughout.

Traces named dev_* carry documented deviations (AGENTS.md release safety): the C++ result is
written explicitly, must differ from the source result, and the source result is what the
Python run produced. A deviation step is followed by an explicit stop when the two states
would otherwise diverge.
"""
import os
import sys
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09h_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.latch import Latch  # noqa: E402
from apps.Orion.modules.pick import Pick  # noqa: E402

# C++ ParamSpec order per descriptor.
PARM_INDEX = {
    "latch": {"polyphony": 0},
    "pick": {"pick_mode": 0, "count": 1, "invert": 2, "offset": 3, "octaves": 4},
}
BOOL_PARMS = {"pick": {"invert"}}


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


def P(ons=(), offs=()):
    return ("p", list(ons), list(offs))


def S(name, value):
    return ("set", name, value)


STOP = ("stop",)


def make(kind):
    transport = Transport()
    helper = Helper(transport, SimpleNamespace(key=0, scale=1))
    if kind == "latch":
        return Latch(helper, 1)
    return Pick(helper, 1)


def apply_set(mod, name, value):
    parm = mod.get_parm_by_name(name)
    assert parm is not None, name
    parm.set_value(value)
    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)


def cpp_set(kind, name, value):
    index = PARM_INDEX[kind][name]
    if name in BOOL_PARMS.get(kind, ()):
        return "  r.set_bool(%d, %s);" % (index, "true" if value else "false")
    return "  r.set_int(%d, %d);" % (index, value)


def fmt_pairs(pairs):
    return ", ".join("{%d, %d}" % (p, v) for p, v in pairs)


def fmt_pitches(pitches):
    return ", ".join(str(p) for p in pitches)


def run_trace(kind, setup, steps, deviations=None):
    deviations = deviations or {}
    mod = make(kind)
    lines = []
    for item in setup:
        apply_set(mod, item[1], item[2])
        lines.append(cpp_set(kind, item[1], item[2]))
    for index, step in enumerate(steps):
        if step[0] == "p":
            _, ons, offs = step
            on_arr = NoteOnArray()
            for pitch, vel in ons:
                assert on_arr.append_value(pitch, velocity=vel)
            off_arr = NoteOffArray()
            for pitch in offs:
                assert off_arr.append_value(pitch)
            out_on, out_off = mod.process(on_arr, off_arr)
            eon = [(out_on.notes[i], out_on.velocities[i]) for i in range(out_on.length)]
            eoff = [out_off.notes[i] for i in range(out_off.length)]
            head = "{%s}, {%s}" % (fmt_pairs(ons), fmt_pitches(offs))
            if index in deviations:
                cpp_on, cpp_off = deviations[index]
                assert [tuple(x) for x in cpp_on] != eon or list(cpp_off) != eoff, "deviation must differ"
                lines.append("  r.step_dev(%s, {%s}, {%s}, {%s}, {%s});" % (
                    head, fmt_pairs(eon), fmt_pitches(eoff), fmt_pairs(cpp_on), fmt_pitches(cpp_off)))
                continue
            lines.append("  r.step(%s, {%s}, {%s});" % (head, fmt_pairs(eon), fmt_pitches(eoff)))
        elif step[0] == "set":
            apply_set(mod, step[1], step[2])
            lines.append(cpp_set(kind, step[1], step[2]))
        elif step[0] == "stop":
            mod.stop()
            lines.append("  r.stop();")
        else:
            raise ValueError(step)
    return lines


# ---------------------------------------------------------------- trace builders

def build_traces():
    # name -> (kind, setup, steps, deviations)
    t = {}

    # Latch (POLY default 12). Incoming note-offs never unlatch; a note-on for a held pitch
    # releases it; a new note-on at capacity evicts the oldest held note first.
    t["latch_basic"] = ("latch", [], [
        P([(60, 100)]), P([], [60]), P([(62, 90)]), P([(60, 80)]), P([], [62]),
        P([(64, 70)]), P([(64, 50)]), P([(62, 40)], [64]),
    ])
    t["latch_evict_poly3"] = ("latch", [S("polyphony", 3)], [
        P([(60, 100)]), P([(62, 90)]), P([(64, 80)]), P([(65, 70)]),
        P([(60, 60)]), P([(62, 50)]), P([(64, 40)]), P([], [60, 62, 64]),
    ])
    # Lowering POLY below the held count evicts one note per note-on, not all of them.
    t["latch_poly_lowered"] = ("latch", [], [
        P([(60, 100)]), P([(61, 100)]), P([(62, 100)]), P([(63, 100)]),
        P([(64, 100)]), P([(65, 100)]), P([(66, 100)]), P([(67, 100)]),
        S("polyphony", 3),
        P([(70, 90)]), P([(71, 90)]), P([(60, 90)]),
    ])
    t["latch_capacity_12"] = ("latch", [], [P([(60 + i, 90)]) for i in range(12)]
                              + [P([(72, 100)]), P([(60, 100)]), P([(61, 100)])])
    t["latch_stop"] = ("latch", [], [
        P([(60, 100)]), P([(62, 100)]), STOP, P([(62, 90)]), P([(60, 80)]),
    ])
    # Same pitch on, toggle-off, on in one batch. Source emits off 60 then on 60 and 62 (60 is
    # left sounding while latch state says released). C++ cancels the pending on 60 and emits
    # nothing for it; the held state is identical to the source.
    t["dev_latch_toggle_same_batch"] = ("latch", [], [
        P([(60, 100), (62, 90), (60, 80), (64, 70)]),
        P([(60, 50)]),
    ], {0: ([(62, 90), (64, 70)], [])})
    # POLY=1: the second note evicts the first in the same batch. Source emits off 60 for a note
    # whose on is still pending in the same batch. C++ cancels that pending on.
    t["dev_latch_evict_same_batch"] = ("latch", [S("polyphony", 1)], [
        P([(60, 100), (62, 90)]),
        P([(64, 80)]),
    ], {0: ([(62, 90)], [])})

    # Pick. Parameters: pick_mode 0..3 (STRT END LOW HIGH), count 1..12, invert, offset 0..12,
    # octaves unbounded in the source.
    t["pick_start_default"] = ("pick", [], [
        P([(60, 100)]), P([(64, 90)]), P([(67, 80)]), P([], [60]), P([], [64]), P([], [67]),
    ])
    t["pick_low_offset_oct"] = ("pick", [S("pick_mode", 2), S("offset", 1), S("octaves", 1)], [
        P([(60, 100), (64, 90), (67, 80)]), P([], [60]), P([], [64]),
    ])
    t["pick_high_count2"] = ("pick", [S("pick_mode", 3), S("count", 2)], [
        P([(60, 100), (64, 90), (67, 80)]), P([], [67]),
    ])
    t["pick_end_count2_offset"] = ("pick", [S("pick_mode", 1), S("count", 2), S("offset", 1)], [
        P([(60, 100), (64, 90), (67, 80)]), S("pick_mode", 0), P(), S("pick_mode", 1), P([], [64]),
    ])
    t["pick_invert_low"] = ("pick", [S("pick_mode", 2), S("invert", True)], [
        P([(60, 100), (64, 90), (67, 80)]), P([], [64]),
    ])
    t["pick_offset_beyond"] = ("pick", [S("pick_mode", 0), S("count", 2), S("offset", 5)], [
        P([(60, 100), (64, 90), (67, 80)]), P([], [60]),
    ])
    # Two holds of pitch 60 (velocities 100 then 80). HIGH reverses a stable sort, so the later
    # hold is selected; the velocity change alone is a selection change.
    t["pick_tie_order_high"] = ("pick", [S("pick_mode", 3)], [
        P([(60, 100)]), P([(60, 80)]), P([], [60]),
    ])
    t["pick_mode_change"] = ("pick", [S("count", 2)], [
        P([(60, 100), (64, 90), (67, 80)]), S("pick_mode", 1), P(), S("pick_mode", 2), P(),
        S("pick_mode", 3), P(), S("pick_mode", 0), P(),
    ])
    t["pick_stop"] = ("pick", [S("count", 2)], [
        P([(60, 100), (64, 90)]), STOP, P([(67, 80)]), P([], [67]),
    ])
    # Twelve held notes fill the list; a thirteenth note-on is dropped by the source.
    t["pick_capacity_12"] = ("pick", [S("pick_mode", 3), S("count", 12)], [
        P([(60 + i, 90) for i in range(12)]), P([(72, 100)]), P([], [71]),
    ])
    # OCT=6 maps 60 to 132. The source emits it (bytearray holds up to 255), which is not a MIDI
    # note. C++ skips it. A stop and OCT=0 follow so both states agree from the next step.
    t["dev_pick_octave_out_of_midi"] = ("pick", [S("octaves", 6)], [
        P([(60, 100)]), STOP, S("octaves", 0), P([(64, 90)]),
    ], {0: ([], [])})
    return t


def main():
    traces = build_traces()
    chunks = [
        "// Generated by tests/native/fixtures/t09h/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source Latch or Pick trace.",
        "// Functions named trace_dev_* carry documented deviations: step_dev() checks the C++",
        "// result and asserts the source (python -I) result differs as recorded here.",
        "",
    ]
    runners = []
    for name in sorted(traces):
        entry = traces[name]
        kind, setup, steps = entry[0], entry[1], entry[2]
        deviations = entry[3] if len(entry) > 3 else {}
        lines = run_trace(kind, setup, steps, deviations)
        chunks.append("void trace_%s(Run& r) {" % name)
        chunks.extend(lines)
        chunks.append("}")
        chunks.append("")
        runners.append((kind, name))
    chunks.append("void run_golden_traces() {")
    for kind, name in runners:
        chunks.append("  {")
        chunks.append("    Run r(%s_module_descriptor());" % kind)
        chunks.append("    trace_%s(r);" % name)
        chunks.append("  }")
    chunks.append("}")
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name in sorted(traces):
        print("%-34s %s steps=%d" % (name, traces[name][0], len(traces[name][2])))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
