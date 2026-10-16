# Host stand-in for CircuitPython's adafruit_ticks (2**29 millisecond wrap).
# Semantics follow the library: ticks_add wraps modulo 2**29, ticks_less uses
# the signed half-period difference. Used only by gen_traces.py under python -I.
TICKS_PERIOD = 1 << 29
TICKS_MAX = TICKS_PERIOD - 1
TICKS_HALFPERIOD = TICKS_PERIOD // 2


def ticks_add(ticks, delta):
    return (ticks + delta) % TICKS_PERIOD


def ticks_diff(ticks1, ticks2):
    diff = (ticks1 - ticks2) & TICKS_MAX
    return ((diff + TICKS_HALFPERIOD) & TICKS_MAX) - TICKS_HALFPERIOD


def ticks_less(ticks1, ticks2):
    return ticks_diff(ticks1, ticks2) < 0
