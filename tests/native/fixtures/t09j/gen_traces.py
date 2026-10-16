"""Generate t09j golden traces by running the Python Wander source under `python -I`.

Usage (from repository root):
  python -I tests/native/fixtures/t09j/gen_traces.py

Writes tests/native/fixtures/t09j_source_traces.inc. Each trace becomes a C++ function that replays
the same steps through the C++ descriptor; expected note-ons/offs are the outputs recorded from
circuitpython-source/src/apps/Orion/modules/wander.py.

Wander has no random source of its own: its "random" choices are the seeded hash
utils.random_int(seed, ...), where seed = SEED + loop step. Those are deterministic, so no random
injection is needed; SEED and the step index are part of each trace.

Time: the source's adafruit_ticks milliseconds wrap at 2**29 (shims/adafruit_ticks.py). Each step
carries an unwrapped millisecond value U. Python sees U mod 2**29; C++ sees (U + 2**32 - 2**29)
mod 2**32, as in t09i.

Transport ticks: the source's midi_tick is unbounded. Each step carries an absolute tick A; C++
receives A mod 2**32 and its unwrapped tick reproduces A, as long as the first tick is below 2**32.

Float parameters: GATE is stored by the source as a Python number. C++ stores float32. Every
GATE value is rounded to float32 before it reaches either side.

Deviation traces (dev_*): the C++ module cannot know, at construction, whether the transport is
running (deviation W2). Each deviation trace is simulated twice: once with the transport running at
construction (the source's behavior) and once stopped (the C++ behavior). The steps where the two
differ become step_dev calls with the source result and the C++ result. Every other step must match
in both simulations.
"""
import os
import struct
import sys
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCE = os.path.join(REPO, "circuitpython-source", "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "t09j_source_traces.inc"))

sys.path.insert(0, os.path.join(HERE, "shims"))
sys.path.insert(0, SOURCE)

from apps.Orion.core.note_array import NoteOnArray, NoteOffArray  # noqa: E402
from apps.Orion.modules.wander import Wander  # noqa: E402

TICKS_PERIOD = 1 << 29
CPP_TICK_MOD = 1 << 32
CPP_OFFSET = (1 << 32) - (1 << 29)

# C++ ParamSpec order (eleven main parameters; the source's create_main_parms order).
PARM_INDEX = {
    "rate": 0, "gravity": 1, "min_step": 2, "max_step": 3, "max_deviation": 4, "tension": 5,
    "rand_seed": 6, "loop_length": 7, "retrigger_mode": 8, "rhythm": 9, "gate": 10,
}
FLOAT_PARMS = {"gate"}


def f32(value):
    """Round to the nearest float32, as the C++ ParamSpec stores it."""
    return struct.unpack("f", struct.pack("f", float(value)))[0]


class Transport:
    def __init__(self, running):
        self.now = 0
        self.midi_tick = 0
        self.running = 1 if running else 0


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


def STOP():
    return ("stop",)


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


def simulate(setup, steps, construct_running):
    """Run the source; return one record per step (in order)."""
    key, scale = 0, 1
    for item in setup:
        if item[0] == "global":
            key, scale = item[1], item[2]
    transport = Transport(construct_running)
    mod = Wander(Helper(transport, key, scale), 2)
    records = [("global", key, scale)]
    for item in setup:
        if item[0] == "set":
            value = f32(item[2]) if item[1] in FLOAT_PARMS else item[2]
            apply_set(mod, item[1], value)
            records.append(("set", item[1], value))
    for step in steps:
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
            assert len(eon) <= 12 and len(eoff) <= 12, "source batch overflow is not a fixture"
            records.append(("p", u, tick, bool(running), list(ons), list(offs), eon, eoff))
        elif kind == "stop":
            mod.stop()
            records.append(("stop",))
        elif kind == "set":
            value = f32(step[2]) if step[1] in FLOAT_PARMS else step[2]
            apply_set(mod, step[1], value)
            records.append(("set", step[1], value))
        else:
            raise ValueError(step)
    return records


def emit(records, deviations=None):
    """Turn records into C++ calls. `deviations` maps a step index to (cpp_on, cpp_off)."""
    deviations = deviations or {}
    lines = []
    for index, record in enumerate(records):
        kind = record[0]
        if kind == "global":
            lines.append("  r.set_global(%d, %d);" % (record[1], record[2]))
        elif kind == "set":
            lines.append(cpp_set(record[1], record[2]))
        elif kind == "stop":
            lines.append("  r.stop();")
        elif kind == "p":
            _, u, tick, running, ons, offs, eon, eoff = record
            head = "%du, %du, %s, {%s}, {%s}" % (
                cpp_now(u), tick % CPP_TICK_MOD, "true" if running else "false",
                fmt_pairs(ons), fmt_pitches(offs))
            if index in deviations:
                cpp_on, cpp_off = deviations[index]
                lines.append("  r.step_dev(%s, {%s}, {%s}, {%s}, {%s});" % (
                    head, fmt_pairs(eon), fmt_pitches(eoff), fmt_pairs(cpp_on), fmt_pitches(cpp_off)))
            else:
                lines.append("  r.step(%s, {%s}, {%s});" % (head, fmt_pairs(eon), fmt_pitches(eoff)))
        else:
            raise ValueError(record)
    return lines


# ---------------------------------------------------------------- trace builders

def ticks(start, stop, *, unit=10, ons=None, offs=None, running=True, base=0):
    """One process call per tick; ons/offs keyed by tick. Time advances `unit` ms per tick."""
    ons = ons or {}
    offs = offs or {}
    return [P(base + t * unit, t, running, ons.get(t, []), offs.get(t, [])) for t in range(start, stop)]


C_MAJOR_TRIAD = [(60, 100)]
DEFAULT_PITCH = {0: [(60, 100)]}


def build():
    t = {}
    # Defaults: RATE=8 (8 ticks), GRAV=50, MIN=1, MAX=2, DEV=8, TENS=0, SEED=0, LOOP=0, STBL, RTHM=0, GATE=100.
    t["defaults"] = ([], ticks(0, 200, ons=DEFAULT_PITCH, offs={150: [60]}))
    t["defaults_held_release_flush"] = ([], ticks(0, 60, ons={0: [(64, 100)]}, offs={50: [64]}))

    # RATE: 1/1 (96 ticks), 1/8 (8 ticks is the default), 1/64 (1 tick).
    t["rate_0"] = ([S("rate", 0)], ticks(0, 400, ons=DEFAULT_PITCH, offs={390: [60]}))
    t["rate_4"] = ([S("rate", 4)], ticks(0, 200, ons=DEFAULT_PITCH, offs={190: [60]}))
    t["rate_12"] = ([S("rate", 12)], ticks(0, 60, ons=DEFAULT_PITCH, offs={55: [60]}))
    t["rate_change_mid_run"] = ([], ticks(0, 40, ons=DEFAULT_PITCH) + [S("rate", 4)]
                                + ticks(40, 120, offs={110: [60]}))

    # GRAV: 0 (no pull), 100 (strong pull); DEV bounds the walk.
    t["gravity_0"] = ([S("gravity", 0), S("max_deviation", 24)],
                      ticks(0, 300, ons={0: [(60, 100)]}, offs={290: [60]}))
    t["gravity_100"] = ([S("gravity", 100), S("max_deviation", 3)],
                        ticks(0, 300, ons={0: [(60, 100)]}, offs={290: [60]}))
    t["gravity_clamp"] = ([S("gravity", 150)], ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))

    # MIN/MAX: zero steps snap a non-scale center to the scale; reversed and clamped values.
    t["min_max_zero_snap"] = ([S("min_step", 0), S("max_step", 0)],
                              ticks(0, 120, ons={0: [(61, 100)]}, offs={110: [61]}))
    t["min_max_wide"] = ([S("min_step", 3), S("max_step", 5), S("max_deviation", 16)],
                         ticks(0, 250, ons=DEFAULT_PITCH, offs={240: [60]}))
    t["min_max_reversed"] = ([S("min_step", 5), S("max_step", 2)],
                             ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))
    t["min_max_clamped"] = ([S("min_step", -3), S("max_step", 40)],
                            ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))

    # DEV: 0 holds the center (TENS adds +1 on the tension step); 1 is a tight leash; 30 clamps to 24.
    t["dev_zero_center"] = ([S("max_deviation", 0)], ticks(0, 120, ons=DEFAULT_PITCH, offs={110: [60]}))
    t["dev_zero_tension"] = ([S("max_deviation", 0), S("tension", 100)],
                             ticks(0, 120, ons=DEFAULT_PITCH, offs={110: [60]}))
    t["dev_one"] = ([S("max_deviation", 1)], ticks(0, 200, ons=DEFAULT_PITCH, offs={190: [60]}))
    t["dev_clamped"] = ([S("max_deviation", 30), S("min_step", 4), S("max_step", 6)],
                        ticks(0, 300, ons=DEFAULT_PITCH, offs={290: [60]}))

    # TENS: tension adds a chromatic step on the tension draw.
    t["tension_50"] = ([S("tension", 50)], ticks(0, 250, ons=DEFAULT_PITCH, offs={240: [60]}))
    t["tension_100_clamp"] = ([S("tension", 250)], ticks(0, 120, ons=DEFAULT_PITCH, offs={110: [60]}))

    # SEED: a different hash stream; SEED above 10000 clamps.
    t["seed_1234"] = ([S("rand_seed", 1234)], ticks(0, 250, ons=DEFAULT_PITCH, offs={240: [60]}))
    t["seed_clamped"] = ([S("rand_seed", 20000)], ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))

    # LOOP: the walk returns to the center every LOOP steps (before the rhythm skip).
    t["loop_1"] = ([S("loop_length", 1)], ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))
    t["loop_3_pattern_skip"] = ([S("loop_length", 3), S("rhythm", 17)],
                                ticks(0, 260, ons={0: [(64, 100)]}, offs={250: [64]}))
    t["loop_clamped"] = ([S("loop_length", 200)], ticks(0, 150, ons=DEFAULT_PITCH, offs={140: [60]}))

    # RTRG: CONT and RTRG advance a step counter; RTRG also restarts the walk on a note-on.
    t["cont_counter"] = ([S("retrigger_mode", 1), S("rhythm", 17)],
                         ticks(0, 220, ons=DEFAULT_PITCH, offs={210: [60]}))
    t["rtrg_restart_on_note_on"] = ([S("retrigger_mode", 2), S("rate", 6)],
                                    ticks(0, 40, ons={0: [(60, 100)], 22: [(67, 90)]}, offs={39: [60]})
                                    + ticks(40, 90, offs={80: [67]}))
    t["stbl_grid_after_restart"] = ([S("retrigger_mode", 0), S("rate", 6)],
                                    ticks(0, 20, ons=DEFAULT_PITCH)
                                    + [P(200 + 10 * k, 20 + k, False) for k in range(0, 6)]
                                    + ticks(26, 60, offs={55: [60]}))
    t["rtrg_transport_restart"] = ([S("retrigger_mode", 2), S("rate", 6)],
                                   ticks(0, 30, ons=DEFAULT_PITCH)
                                   + [P(300 + 10 * k, 30 + k, False) for k in range(0, 5)]
                                   + ticks(35, 70, offs={65: [60]}))
    t["cont_transport_restart"] = ([S("retrigger_mode", 1), S("rate", 6)],
                                   ticks(0, 30, ons=DEFAULT_PITCH)
                                   + [P(300 + 10 * k, 30 + k, False) for k in range(0, 5)]
                                   + ticks(35, 70, offs={65: [60]}))

    # RTHM: pattern 3 (len 3), pattern 17 (len 8 with a skipped step), pattern 24.
    t["rhythm_3"] = ([S("rhythm", 3)], ticks(0, 200, ons=DEFAULT_PITCH, offs={190: [60]}))
    t["rhythm_24"] = ([S("rhythm", 24)], ticks(0, 260, ons=DEFAULT_PITCH, offs={250: [60]}))

    # GATE: zero releases on the next pass; shorter than a bin; longer than a bin; negative; fraction.
    t["gate_0"] = ([S("gate", 0)], ticks(0, 100, ons=DEFAULT_PITCH, offs={90: [60]}))
    t["gate_25_shorter_than_bin"] = ([S("gate", 25)], ticks(0, 120, ons=DEFAULT_PITCH, offs={110: [60]}))
    t["gate_500_overlap"] = ([S("gate", 500)], ticks(0, 120, ons=DEFAULT_PITCH, offs={110: [60]}))
    t["gate_negative"] = ([S("gate", -20)], ticks(0, 100, ons=DEFAULT_PITCH, offs={90: [60]}))
    t["gate_fraction"] = ([S("gate", 37.5)], ticks(0, 100, ons=DEFAULT_PITCH, offs={90: [60]}))

    # Key and scale: pentatonic in D, chromatic, and a minor key with a non-scale center.
    t["key_d_pentatonic"] = ([G(2, 14), S("min_step", 1), S("max_step", 3)],
                             ticks(0, 180, ons={0: [(62, 100)]}, offs={170: [62]}))
    t["chromatic_scale"] = ([G(0, 0)], ticks(0, 120, ons={0: [(61, 100)]}, offs={110: [61]}))
    t["key_fsharp_minor"] = ([G(6, 2)], ticks(0, 180, ons={0: [(70, 100)]}, offs={170: [70]}))

    # Pitch edges: the walk clamps at 0 and 127.
    t["edge_low_clamp"] = ([S("min_step", 3), S("max_step", 8), S("max_deviation", 24)],
                           ticks(0, 200, ons={0: [(0, 100)]}, offs={190: [0]}))
    t["edge_high_clamp"] = ([S("min_step", 3), S("max_step", 8), S("max_deviation", 24)],
                            ticks(0, 200, ons={0: [(127, 100)]}, offs={190: [127]}))

    # Held notes: a 13th hold is not recorded; the center is the newest held note.
    t["held_13_overflow"] = ([], [P(k * 10, k, True, [(40 + k, 100)] if k < 13 else [], [])
                                  for k in range(0, 60)])
    t["same_batch_on_off"] = ([], ticks(0, 40, ons={0: [(60, 100)]}, offs={0: [60]}))
    t["center_release_moves"] = ([], ticks(0, 120, ons={0: [(60, 100)], 10: [(64, 100)]},
                                           offs={40: [64]}))
    t["repress_moves_newest"] = ([], ticks(0, 120, ons={0: [(60, 100), (64, 100)], 30: [(60, 100)]},
                                           offs={70: [60]}))
    t["velocity_follows_last_on"] = ([], ticks(0, 120, ons={0: [(30, 20)], 50: [(60, 90)]},
                                               offs={110: [60]}))
    t["unknown_off_ignored"] = ([], ticks(0, 60, ons=DEFAULT_PITCH, offs={5: [99], 50: [60]}))

    # Ticks: a jump skips bins (one note per pass); a backwards restart resets the grid.
    t["tick_jump"] = ([S("rate", 6)], [P(0, 0, True, [(60, 100)], []), P(10, 1, True),
                                       P(20, 40, True), P(30, 41, True), P(40, 47, True),
                                       P(50, 120, True, [], [60])])
    t["tick_backwards"] = ([S("rate", 6)], ticks(0, 30, ons={0: [(60, 100)]}, offs={29: [60]})
                           + [P(400 + 10 * k, 5 + k, True) for k in range(0, 20)])

    # Stop: held notes are cleared; the sounding note is dropped (the controller releases it).
    t["stop_mid_note"] = ([], ticks(0, 15, ons=DEFAULT_PITCH) + [STOP()]
                           + ticks(15, 40, offs={30: [60]})
                           + ticks(40, 60, ons={42: [(64, 100)]}, offs={58: [64]}))

    # Millisecond wrap at 2**29 with a gate deadline crossing it.
    wrap = TICKS_PERIOD - 35
    t["ms_wrap_gate_deadline"] = ([S("gate", 60), S("rate", 6)],
                                  [P(wrap + k * 10, k, True, [(60, 100)] if k == 0 else [], [])
                                   for k in range(0, 14)])

    # Absolute tick above 2**32 (the source's midi_tick is unbounded; C++ unwraps 32-bit deltas).
    base = (1 << 32) - 40
    t["tick_crosses_2_32"] = ([S("rate", 6), S("rhythm", 17)],
                              [P(k * 10, base + k, True, [(60, 100)] if k == 0 else [],
                                 [60] if k == 150 else []) for k in range(0, 160)])

    # Deviation W2: the source takes transport.running at construction; C++ starts stopped.
    t["dev_running_at_construction"] = ([S("retrigger_mode", 2)],
                                        [P(0, 0, True, [(60, 100)], []), STOP(),
                                         P(10, 1, True), P(20, 2, True)])
    return t


DEVIATION_TRACES = {"dev_running_at_construction"}


def trace_lines(name, setup, steps):
    if name in DEVIATION_TRACES:
        source = simulate(setup, steps, True)
        cpp = simulate(setup, steps, False)
        assert len(source) == len(cpp)
        deviations = {}
        for index, (src, cur) in enumerate(zip(source, cpp)):
            if src != cur:
                assert src[0] == "p" and cur[0] == "p", "only pass results may deviate"
                deviations[index] = (cur[6], cur[7])
        assert deviations, "a deviation trace must differ from the source"
        # Source records carry the step_dev source result; the deviations carry the C++ result.
        return emit(source, deviations), len(steps), len(deviations)
    records = simulate(setup, steps, False)
    return emit(records), len(steps), 0


def main():
    traces = build()
    chunks = [
        "// Generated by tests/native/fixtures/t09j/gen_traces.py under python -I.",
        "// Do not edit by hand. Each function replays one circuitpython-source Wander trace.",
        "// Trace results were produced by python -I. Deviation traces (dev_*) carry the source result",
        "// in step_dev and the C++ result (deviation W2); every other step matches the source.",
        "",
    ]
    names = []
    summary = []
    for name in sorted(traces):
        setup, steps = traces[name]
        lines, count, dev_count = trace_lines(name, setup, steps)
        names.append(name)
        chunks.append("void trace_wander_%s(Run& r) {" % name)
        chunks.extend(lines)
        chunks.append("}")
        chunks.append("")
        summary.append((name, count, dev_count))
    chunks.append("const NamedTrace kWanderTraces[] = {")
    for name in names:
        chunks.append('    {"%s", trace_wander_%s},' % (name, name))
    chunks.append("};")
    chunks.append("")
    with open(OUT, "w", newline="\n") as handle:
        handle.write("\n".join(chunks) + "\n")
    for name, count, dev_count in summary:
        print("%-32s steps=%-4d deviations=%d" % (name, count, dev_count))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
