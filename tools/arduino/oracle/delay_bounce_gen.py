# Generates C++ fixture statements from the CircuitPython oracle (python -I).
import sys
ORION_DIR = sys.argv[1]
OUT = sys.argv[2]
sys.argv = ["x", ORION_DIR]
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "delay_bounce_harness.py")).read())

DELAY_P = {"delay": 0, "delay_random": 1}
BOUNCE_P = {"bounces": 0, "interval_mode": 1, "interval_ms": 2, "interval_rate": 3, "gate": 4,
            "interval_mod": 5, "velocity_mod": 6, "gate_mod": 7, "mod_curve": 8}
FLOAT_NAMES = {"gate", "interval_mod", "velocity_mod", "gate_mod"}

def cpp_float(x):
    return repr(f32(x)) + "f"

scen = []
def S(name, kind, base_cpp, steps):
    scen.append((name, kind, base_cpp, steps))

P = lambda rel, tick, ons=(), offs=(): ("proc", rel, tick, list(ons), list(offs))
SET = lambda name, v: ("set", name, v)

# ---- delay ----
S("DelayBasic", "delay", 1000, [
    SET("delay", 100), SET("delay_random", 0),
    P(0, 1, [(60, 100)]), P(50, 2, [], [60]), P(99, 3), P(100, 4), P(101, 5), P(149, 6), P(150, 7), P(151, 8),
])
S("DelayRetrigger", "delay", 0, [
    SET("delay", 50), SET("delay_random", 0),
    P(0, 1, [(60, 90)]), P(10, 2, [], [60]), P(20, 3, [(60, 80)]), P(30, 4, [], [60]),
    P(40, 5), P(51, 6), P(61, 7), P(70, 8, [], [60]), P(80, 9), P(120, 10), P(121, 11),
])
S("DelayRandomMatched", "delay", 0, [
    SET("delay", 100), SET("delay_random", 40),
    P(0, 7, [(60, 100), (64, 90), (67, 80)]), P(20, 8, [(72, 70)]),
    P(40, 9, [], [60, 64]), P(60, 10, [], [67, 72]),
    P(100, 11), P(120, 12), P(140, 13), P(160, 14), P(200, 15), P(300, 16),
])
S("DelayUnmatchedOffUsesDelay", "delay", 0, [
    SET("delay", 30), SET("delay_random", 50),
    P(0, 3, [(60, 100)]), P(5, 4, [], [72]), P(10, 5, [], [60]), P(20, 6), P(40, 7), P(60, 8), P(80, 9), P(120, 10),
])
S("DelayPendingOnOverflow", "delay", 0, [
    SET("delay", 1000), SET("delay_random", 0),
    P(0, 1, [(p, 100) for p in range(48, 60)]),
    P(1, 2, [(61, 100)]), P(2, 3, [], [61]),
    P(999, 4), P(1000, 5), P(1001, 6), P(1002, 7), P(1003, 8),
])
S("DelayWrapAround", "delay", 0xFFFFFFE2, [
    SET("delay", 20), SET("delay_random", 0),
    P(0, 1, [(60, 100)]), P(10, 2, [], [60]), P(19, 3), P(20, 4), P(21, 5, [(62, 90)]), P(31, 6), P(41, 7, [], [62]),
    P(42, 8),
])
S("DelayStopClears", "delay", 0, [
    SET("delay", 100), SET("delay_random", 0),
    P(0, 1, [(60, 100)]), P(10, 2, [], [60]), ("stop",), P(200, 3), P(300, 4),
])

# ---- bounce ----
S("BounceDefaultMs", "bounce", 0, [
    P(0, 1, [(60, 100)]), P(50, 2), P(150, 3), P(151, 4), P(300, 5), P(450, 6), P(600, 7),
])
S("BounceRateMode", "bounce", 0, [
    SET("interval_mode", 1), SET("interval_rate", 8), SET("bounces", 3), SET("gate", 100.0),
    P(0, 10, [(60, 100), (64, 60)]), P(5, 12), P(6, 16), P(7, 17), P(10, 18), P(12, 22), P(20, 30), P(30, 40),
])
S("BounceCurveMods", "bounce", 0, [
    SET("bounces", 4), SET("interval_ms", 120), SET("gate", 80.0), SET("interval_mod", 0.5),
    SET("velocity_mod", 0.75), SET("gate_mod", 0.5), SET("mod_curve", 1),
    P(0, 1, [(60, 120)]), P(120, 2), P(200, 3), P(260, 4), P(300, 5), P(400, 6), P(500, 7), P(600, 8),
])
S("BounceRandCurveSmallMods", "bounce", 0, [
    SET("bounces", 5), SET("interval_ms", 90), SET("gate", 60.0), SET("interval_mod", f32(0.05)),
    SET("velocity_mod", f32(0.05)), SET("gate_mod", f32(0.05)), SET("mod_curve", 2),
    P(0, 9, [(60, 100), (67, 77)]), P(90, 10), P(120, 11), P(180, 12), P(200, 13), P(260, 14), P(300, 15),
    P(400, 16), P(500, 17), P(600, 18),
])
S("BounceQueueEviction", "bounce", 0, [
    SET("bounces", 64), SET("interval_ms", 100000), SET("gate", 10.0),
] + [P(i, i + 1, [(48 + i, 100 - i)]) for i in range(14)] + [P(50, 20), P(20000, 21)])
S("BounceZeroThenStop", "bounce", 0, [
    SET("bounces", 0),
    P(0, 1, [(60, 100)]), P(10, 2),
    SET("bounces", 2),
    P(20, 3, [(62, 100)]), ("stop",), P(200, 4), P(400, 5),
])

S("BounceWrapAround", "bounce", 0xFFFFFFE2, [
    SET("bounces", 2), SET("interval_ms", 60), SET("gate", 40.0),
    P(0, 1, [(60, 100)]), P(40, 2), P(60, 3), P(61, 4), P(90, 5), P(121, 6), P(122, 7),
])
S("BounceRateWrapCheck", "bounce", 0, [
    SET("interval_mode", 1), SET("interval_rate", 11), SET("bounces", 2), SET("gate", 10.0),
    P(0, 5, [(70, 100)]), P(1, 6), P(2, 7), P(3, 8), P(4, 9),
])

S("DelayRetriggerSameCall", "delay", 0, [
    SET("delay", 50), SET("delay_random", 0),
    P(0, 1, [(60, 100)]), P(10, 2, [(60, 90)], [60]), P(51, 3), P(60, 4), P(61, 5), P(62, 6), P(110, 7), P(111, 8),
])
S("DelayUnrecordedAfterSet", "delay", 0, [
    SET("delay", 10), SET("delay_random", 0),
    P(0, 1, [(60, 100)]), SET("delay", 90), P(1, 2, [(60, 100)]), P(2, 3, [], [60]),
    P(11, 4), P(13, 5), P(14, 6), P(91, 7), P(92, 8), P(200, 9, [], [60]), P(290, 10), P(291, 11),
])
def py_steps(steps, base):
    res = []
    for st in steps:
        if st[0] == "proc":
            _, rel, tick, ons, offs = st
            res.append(("process", (base + rel) % MAX if base >= 0 else 0, tick, ons, offs))
        else:
            res.append(st)
    return res

out = []
out.append("// Generated from circuitpython-source/src/apps/Orion/modules/{delay,bounce}.py by running the")
out.append("// source under python -I with adafruit_ticks semantics (2^29 wrap, signed half-range compare).")
out.append("// Do not edit by hand; regenerate with the harness in the migration workspace.")
for name, kind, base_cpp, steps in scen:
    base_py = (MAX - 30) if base_cpp == 0xFFFFFFE2 else base_cpp
    if base_cpp == 0xFFFFFFE2:
        base_py = MAX - 30
    pysteps = []
    for st in steps:
        if st[0] == "proc":
            _, rel, tick, ons, offs = st
            pysteps.append(("process", (base_py + rel) % MAX, tick, ons, offs))
        elif st[0] == "set":
            pysteps.append(st)
        else:
            pysteps.append(st)
    res = run(kind, pysteps)
    out.append(f"// ---- {name} ----")
    out.append(f"void run_{name}(Harness& h) {{")
    ri = 0
    pmap = DELAY_P if kind == "delay" else BOUNCE_P
    for st in steps:
        if st[0] == "set":
            _, pname, v = st
            if pname in FLOAT_NAMES:
                lit = cpp_float(v)
                out.append(f"  h.setf({pmap[pname]}, {lit});  // {pname}")
            else:
                out.append(f"  h.seti({pmap[pname]}, {int(v)});  // {pname}")
        elif st[0] == "stop":
            out.append("  h.stop();")
        else:
            _, rel, tick, ons, offs = st
            on_list, off_list = res[ri]; ri += 1
            ons_s = ", ".join(f"{{{p}, {v}}}" for p, v in ons)
            offs_s = ", ".join(str(p) for p in offs)
            cnow = (base_cpp + rel) & 0xFFFFFFFF
            out.append(f"  h.step({cnow}u, {tick}, {{{ons_s}}}, {{{offs_s}}});")
            eon = ", ".join(f"{{{p}, {v}}}" for p, v in on_list)
            eoff = ", ".join(str(p) for p in off_list)
            out.append(f"  expect(h, {{{eon}}}, {{{eoff}}});")
    out.append("}")
open(OUT, "w").write("\n".join(out) + "\n")
print("ok", len(scen))
