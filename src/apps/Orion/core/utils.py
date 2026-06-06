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
