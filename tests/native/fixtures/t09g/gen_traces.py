"""Generate t09g golden traces for the euclid processor by running the Python source.

Usage (from repository root):
  python -I tests/native/fixtures/t09g/gen_traces.py

Writes tests/native/fixtures/t09g_source_traces.inc (traces and the euclid_step table).
Each trace replays the same steps through the C++ descriptor; expected outputs are the
ordered note-ons/offs recorded from circuitpython-source/src/apps/Orion/modules/euclid.py.
Parameter setters follow scenes.load_scene: parm.set_value() then the edit callback.

Time: each step carries an unwrapped millisecond value U. Python sees U mod 2**29 through
the t09d adafruit_ticks shim; C++ sees (U + 2**32 - 2**29) mod 2**32 (same wrap rule).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09g_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "..", "t09d", "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.euclid import Euclid, euclid_step  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_OFFSET = (1 << 32) - (1 << 29)
T = True
F = False

# C++ parameter indices must match kEuclidParams order.
PARM_INDEX = {"lane": 0, "rate": 1, "pulses": 2, "length": 3, "rotation": 4,
              "sort": 5, "gate": 6, "retrigger_mode": 7}


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


def S(name, value):
    return ("set", name, value)


STOP = ("stop",)


def set_parm(mod, name, value):
    """Mirror scenes.load_scene: set_value (clamps, binds attrs), then the edit callback."""
    parm = mod.get_parm_by_name(name)
    parm.set_value(value)
    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)


def cpp_set(name, value):
    index = PARM_INDEX[name]
    if name == "gate":
        return "  r.set_float(%d, %sf);" % (index, repr(float(value)))
    return "  r.set_int(%d, %d);" % (index, value)


def fmt_notes(pairs):
    return ", ".join("{%d, %d}" % (p, v) for p, v in pairs)


def fmt_pitches(pitches):
    return ", ".join(str(p) for p in pitches)


def lane_setup(lane, length, pulses, rotation):
    """Set one lane in scene-load order (length first, so pulses may exceed it)."""
    return [("lane", lane), ("length", length), ("pulses", pulses), ("rotation", rotation)]


def run_trace(setup, steps):
    transport = Transport()
    mod = Euclid(Helper(transport), 2)
    lines = []
    for name, value in setup:
        set_parm(mod, name, value)
        lines.append(cpp_set(name, value))
    for step in steps:
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
            assert len(eon) <= 6 and len(eoff) <= 6, "source output exceeds six lanes"
            lines.append("  r.step(%du, %d, %s, {%s}, {%s}, {%s}, {%s});" % (
                cpp_now(u), tick, "true" if running else "false", fmt_notes(ons), fmt_pitches(offs),
                fmt_notes(eon), fmt_pitches(eoff)))
        elif step[0] == "stop":
            mod.stop()
            lines.append("  r.stop();")
        elif step[0] == "set":
            set_parm(mod, step[1], step[2])
            lines.append(cpp_set(step[1], step[2]))
        else:
            raise ValueError(step)
    return lines


def traces():
    t = {}
    # Defaults: LANE=0, RATE=8, PULSES=4, LENGTH=16, ROT=0, SORT=UP, GATE=100, RTRG=STBL.
    t["defaults_three_notes"] = ([], [
        P(0, 0, T, [(67, 100)]), P(25, 0, T, [(60, 90)]), P(50, 0, T, [(64, 80)])]
        + [P(25 * u, 6 * u, T) for u in range(3, 40)])

    # Six lanes with distinct length/pulse/rotation patterns; lane 4 stores pulses above length.
    t["lane_patterns"] = (
        lane_setup(0, 8, 5, 0) + lane_setup(1, 7, 3, 2) + lane_setup(2, 16, 16, 0)
        + lane_setup(3, 5, 0, 0) + lane_setup(4, 8, 20, 0) + lane_setup(5, 16, 4, -3)
        + [("lane", 0), ("rotation", 127)],
        [P(0, 0, T, [(60, 100), (61, 100), (62, 100), (63, 100), (64, 100), (65, 100)])]
        + [P(25 * u, 6 * u, T) for u in range(1, 50)])

    t["gate_30"] = ([("gate", 30)], [P(0, 0, T, [(60, 100)])] + [P(10 * u, 6 * u, T) for u in range(1, 30)])
    t["gate_zero"] = ([("gate", 0)], [P(0, 0, T, [(60, 100)])] + [P(10 * u, 6 * u, T) for u in range(1, 12)])
    t["gate_fractional"] = ([("gate", 33.7)], [P(0, 0, T, [(60, 100)])] + [P(10 * u, 6 * u, T) for u in range(1, 20)])
    t["gate_change_running"] = ([], [P(0, 0, T, [(60, 100)])] + [P(10 * u, 6 * u, T) for u in range(1, 7)]
                                + [S("gate", 15)] + [P(10 * u, 6 * u, T) for u in range(7, 20)])

    t["length_clamps_pulses"] = (
        [("pulses", 10)],
        [P(0, 0, T, [(60, 100)])] + [P(25 * u, 6 * u, T) for u in range(1, 8)]
        + [S("length", 6)] + [P(25 * u, 6 * u, T) for u in range(8, 24)]
        + [S("lane", 1), S("pulses", 1), S("lane", 0)] + [P(25 * u, 6 * u, T) for u in range(24, 30)])

    # RTRG: a note-on from silence restarts the step counter; a transport start restarts it too.
    t["rtrg_restart"] = (
        [("retrigger_mode", 2), ("length", 8), ("pulses", 5)],
        [P(0, 0, F, [(60, 100)])]
        + [P(10 * u + 10, 6 * u, T) for u in range(1, 20)]
        + [P(300 + 10 * u, 6 * (20 + u), F) for u in range(0, 3)]
        + [P(400 + 10 * u, 6 * (30 + u), T) for u in range(0, 20)]
        + [P(600, 6 * 50, T, [], [60]), P(610, 6 * 51, T)])

    # CONT: the step counter advances once per grid change, not per pass.
    t["cont_repeated_passes"] = ([("retrigger_mode", 1)], [P(0, 0, T, [(60, 100)])]
        + [P(3 * u, 3 * u, T) for u in range(1, 60)]
        + [P(200, 6 * 60, T, [(62, 100)]), P(210, 6 * 61, T, [], [60])]
        + [P(220 + u, 6 * 61 + u, T) for u in range(1, 10)])

    # STBL uses the grid bin as the step: a forward jump skips bins, a backwards tick restarts the grid.
    t["stbl_jump_backwards"] = ([], [P(0, 0, T, [(60, 100)]), P(10, 60, T), P(20, 66, T), P(30, 30, T),
                                     P(40, 36, T), P(50, 36, T), P(60, 42, T), P(70, 48, T), P(80, 54, T)])

    # Held-note reassignment: one pitch can be held by two lanes, so one release emits two offs.
    reassign = [P(0, 0, T, [(60, 100), (62, 100)])]
    for b in range(1, 15):
        ons = [(58, 100)] if b == 2 else []
        offs = [58] if b == 4 else [60] if b == 9 else []
        reassign.append(P(5 * b, 6 * b, T, ons, offs))
    reassign.append(P(5 * 15, 6 * 15, T))
    t["reassign_multiplicity"] = (
        lane_setup(0, 2, 1, 0) + lane_setup(1, 2, 1, 1) + lane_setup(2, 1, 0, 0) + [("lane", 0)],
        reassign)

    # Two lanes release the same pitch in one pass (source emits off 60 twice at b=5). Found by a
    # seeded search over lane settings; the lane setup is fixed here so the trace is reproducible.
    t["double_release"] = (
        [("lane", 0), ("length", 3), ("pulses", 1), ("rotation", 0),
         ("lane", 1), ("length", 4), ("pulses", 4), ("rotation", 3),
         ("lane", 2), ("length", 4), ("pulses", 3), ("rotation", 3),
         ("lane", 3), ("length", 1), ("pulses", 3), ("rotation", 3),
         ("lane", 4), ("length", 1), ("pulses", 1), ("rotation", 0),
         ("lane", 5), ("length", 2), ("pulses", 3), ("rotation", 1),
         ("lane", 0), ("sort", 0)],
        [P(5 * b, 6 * b, T, [(60, 100)] if b == 0 else [(58, 100)] if b == 3 else [(64, 100)] if b == 8
           else [(66, 100)] if b == 11 else [],
           [60] if b == 0 else [58] if b == 3 else [64] if b == 10 else [66] if b == 13 else [])
         for b in range(0, 16)])
    t["sort_down"] = ([("sort", 1)], [P(0, 0, T, [(60, 100), (64, 90), (62, 80), (67, 70)])]
                      + [P(25 * u, 6 * u, T) for u in range(1, 40)])
    t["sort_ord"] = ([("sort", 2)], [P(0, 0, T, [(64, 100)]), P(1, 0, T, [(60, 90)]), P(2, 0, T, [(67, 80)])]
                     + [P(25 * u, 6 * u, T) for u in range(1, 40)])

    # Thirteenth held note is dropped (source append returns False); re-press moves a note to the end.
    t["held_capacity"] = ([], [P(0, 0, T, [(48 + i, 100) for i in range(12)]), P(1, 6, T, [(72, 100)]),
                               P(2, 12, T, [(50, 50)])]
                          + [P(25 * u, 6 * u, T) for u in range(3, 30)])

    t["stop_reset"] = ([], [P(0, 0, T, [(60, 100), (64, 100)]), P(5, 6, T), P(10, 12, T), STOP,
                            P(15, 18, T), P(20, 24, T, [(67, 100)])]
                       + [P(25 * u, 6 * (u + 4), T) for u in range(2, 25)])

    t["transport_stopped_release"] = ([], [P(0, 0, F, [(60, 100)]), P(1, 6, F, [], [60]),
                                           P(2, 12, T, [(62, 100)]), P(3, 18, T), P(4, 24, T), P(5, 30, F)]
                                      + [P(25 * u, 6 * u, T) for u in range(6, 12)])

    t["wrap_gate"] = ([], [P(TICKS_PERIOD - 30 + 5 * k, 6 * k, T, [(60, 100)] if k == 0 else [])
                           for k in range(0, 40)])
    return t


def euclid_table():
    """Per (steps, pulses): bit s is set when euclid_step(s, pulses, steps) is true (steps <= 32)."""
    rows = []
    for steps in range(1, 33):
        for pulses in range(0, steps + 1):
            mask = 0
            for s in range(steps):
                if euclid_step(s, pulses, steps):
                    mask |= 1 << s
            rows.append((steps, pulses, mask))
    return rows


def long_count_table():
    """Hit counts for long lengths (the mask does not fit 64 bits); each count must equal pulses."""
    rows = []
    for steps in (33, 64, 100, 127, 128):
        for pulses in range(0, steps + 1):
            hits = sum(1 for s in range(steps) if euclid_step(s, pulses, steps))
            assert hits == pulses, (steps, pulses, hits)
            rows.append((steps, pulses, hits))
    return rows


def main():
    chunks = [
        "// Generated by tests/native/fixtures/t09g/gen_traces.py under python -I.",
        "// Do not edit by hand. Source: circuitpython-source/src/apps/Orion/modules/euclid.py.",
        "",
        "struct EuclidTableRow { std::uint16_t steps; std::uint16_t pulses; std::uint64_t mask; };",
        "constexpr EuclidTableRow kEuclidStepTable[] = {",
    ]
    rows = euclid_table()
    for steps, pulses, mask in rows:
        chunks.append("    {%d, %d, 0x%xULL}," % (steps, pulses, mask))
    chunks.append("};")
    chunks.append("")
    chunks.append("struct EuclidCountRow { std::uint16_t steps; std::uint16_t pulses; std::uint16_t hits; };")
    chunks.append("constexpr EuclidCountRow kEuclidCountTable[] = {")
    long_rows = long_count_table()
    for steps, pulses, hits in long_rows:
        chunks.append("    {%d, %d, %d}," % (steps, pulses, hits))
    chunks.append("};")
    chunks.append("")
    summary = []
    for name, (setup, steps) in sorted(traces().items()):
        chunks.append("void trace_%s(Run& r) {" % name)
        chunks.extend(run_trace(setup, steps))
        chunks.append("}")
        chunks.append("")
        summary.append((name, len(steps)))
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name, count in summary:
        print("%-28s steps=%d" % (name, count))
    print("mask rows=%d count rows=%d" % (len(rows), len(long_rows)))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
