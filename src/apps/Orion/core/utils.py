### RANDOM HELPERS ###

'''
Deterministic pseudo-random helpers used by timing and note modules.
'''

def hash_u32(value):
    value = (value ^ 61) ^ (value >> 16)
    value = value + (value << 3)
    value = value ^ (value >> 4)
    value = value * 0x27D4EB2D
    value = value ^ (value >> 15)
    return value & 0xFFFFFFFF

def random_float(seed):
    return hash_u32(seed) / 0x100000000

def random_int(seed, max_value, mode):
    """
    mode 0 returns 0..max_value, mode 1 returns -max_value..max_value.
    """
    max_value = int(max_value)
    if mode == 0:
        low = min(0, max_value)
        high = max(0, max_value)
    else:
        low = min(-max_value, max_value)
        high = max(-max_value, max_value)
    return low + (hash_u32(seed) % (high - low + 1))


### GRID HELPERS ###

def grid_change(midi_tick, rate_ticks, last_grid_bin):
    current_bin = midi_tick // rate_ticks
    if current_bin < last_grid_bin:
        last_grid_bin = -1
    if current_bin == last_grid_bin:
        return current_bin, False, 0
    triggers = 1 if last_grid_bin < 0 else current_bin - last_grid_bin
    return current_bin, True, triggers


### BOUNDARY HELPERS ###

BOUNDARY_LIST = ("WRPU", "WRPB", "FLDU", "FLDB", "CLMP")
BOUNDARY_WRAP_UNI, BOUNDARY_WRAP_BI, BOUNDARY_FOLD_UNI = 0, 1, 2
BOUNDARY_FOLD_BI, BOUNDARY_CLAMP = 3, 4

def bound_offset(offset, limit, mode):
    if limit <= 0:
        return offset
    if mode == BOUNDARY_CLAMP:
        return max(-limit, min(limit, offset))
    if mode == BOUNDARY_WRAP_UNI:
        return offset % (limit + 1)
    if mode == BOUNDARY_WRAP_BI:
        return ((offset + limit) % (limit * 2 + 1)) - limit

    if mode == BOUNDARY_FOLD_UNI:
        period = limit * 2
        offset = offset % period
        if offset <= limit:
            return offset
        return period - offset

    period = limit * 4
    offset = offset % period
    if offset <= limit:
        return offset
    if offset <= limit * 3:
        return limit * 2 - offset
    return offset - period
