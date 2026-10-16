# Oracle harness: runs the CircuitPython delay/bounce source under python -I.
# Usage: python -I harness.py <repo_src_apps_Orion_dir> <scenario-name>
import sys, types, struct

ORION = sys.argv[1]
MAX = 1 << 29  # adafruit_ticks wrap (as recalled; .mpy not decompiled here)

def _diff(a, b):
    d = (a - b) % MAX
    if d >= MAX // 2:
        d -= MAX
    return d

ticks = types.ModuleType("adafruit_ticks")
ticks.ticks_add = lambda t, d: (t + d) % MAX
ticks.ticks_less = lambda a, b: _diff(a, b) < 0
sys.modules["adafruit_ticks"] = ticks

root = types.ModuleType("orion"); root.__path__ = [ORION]
core = types.ModuleType("orion.core"); core.__path__ = [ORION + "/core"]
mods = types.ModuleType("orion.modules"); mods.__path__ = [ORION + "/modules"]
for m in (root, core, mods):
    sys.modules[m.__name__] = m
root.core = core; root.modules = mods

import importlib
delay_mod = importlib.import_module("orion.modules.delay")
bounce_mod = importlib.import_module("orion.modules.bounce")
from orion.core.note_array import NoteOnArray, NoteOffArray

class Transport: pass
class Helper:
    def __init__(self, t):
        self.transport = t
        self.output_manager = None
        self.state = None
        self.ui_manager = None

def f32(x):  # round to float32, as ParamValue carries float
    return struct.unpack("f", struct.pack("f", x))[0]

def make(kind):
    t = Transport(); t.now = 0; t.midi_tick = 0
    h = Helper(t)
    if kind == "delay":
        m = delay_mod.Delay(h, 1)
    else:
        m = bounce_mod.Bounce(h, 1)
    return m, t

def run(kind, steps):
    m, t = make(kind)
    out = []
    for st in steps:
        op = st[0]
        if op == "set":
            name, value = st[1], st[2]
            if name == "interval_rate":
                m.set_interval_rate(value)
            else:
                setattr(m, name, value)
        elif op == "stop":
            m.stop()
        elif op == "process":
            _, now, tick, ons, offs = st
            t.now = now; t.midi_tick = tick
            on_arr = NoteOnArray(); off_arr = NoteOffArray()
            for (p, v) in ons: on_arr.append_value(p, velocity=v)
            for p in offs: off_arr.append_value(p)
            ro, rf = m.process(on_arr, off_arr)
            on_list = [(ro.notes[i], ro.velocities[i]) for i in range(ro.length)]
            off_list = [rf.notes[i] for i in range(rf.length)]
            out.append((on_list, off_list))
    return out

if __name__ == "__main__":
    pass
