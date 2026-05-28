MODULE_SPECS = (
    ("empty", "EMPTY"),
    ("arp", "ARP"),
    ("transpose", "TRNS"),
    ("pick", "PICK"),
    ("chord", "CHRD"),
    ("randomize", "RND"),
)

AVAILABLE_MODULE_NAMES = [spec[0] for spec in MODULE_SPECS]
AVAILABLE_MODULE_LABELS = [spec[1] for spec in MODULE_SPECS]


def get_module_class(module_key):
    if module_key == "empty":
        from ..core._modules._empty import Empty
        return Empty
    if module_key == "arp":
        from .arp import Arp
        return Arp
    if module_key == "transpose":
        from .transpose import Transpose
        return Transpose
    if module_key == "pick":
        from .pick import Pick
        return Pick
    if module_key == "chord":
        from .chord import Chord
        return Chord
    if module_key == "randomize":
        from .randomize import Randomize
        return Randomize
    return None
