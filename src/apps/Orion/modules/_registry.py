MODULE_SPECS = (
    ("empty", "EMPTY"),
    ("arp", "ARP"),
    ("bounce", "BOUNCE"),
    ("chord", "CHRD"),
    ("delay", "DELAY"),
    ("gate", "GATE"),
    ("latch", "LATCH"),
    ("pick", "PICK"),
    ("quantize", "QNT"),
    ("randomize", "RND"),
    ("strum", "STRUM"),
    ("transpose", "TRNS"),
    ("wander", "WANDR")
)

AVAILABLE_MODULE_NAMES = [spec[0] for spec in MODULE_SPECS]
AVAILABLE_MODULE_LABELS = [spec[1] for spec in MODULE_SPECS]

MODULE_IMPORT_NAMES = {
    "arp": ".arp",
    "bounce": ".bounce",
    "chord": ".chord",
    "delay": ".delay",
    "gate": ".gate",
    "latch": ".latch",
    "pick": ".pick",
    "quantize": ".quantize",
    "randomize": ".randomize",
    "strum": ".strum",
    "transpose": ".transpose",
    "wander": ".wander",
}


def get_module_class(module_key):
    import gc
    gc.collect()

    if module_key == "empty":
        from ..core._modules._empty import Empty
        return Empty
    if module_key == "arp":
        from .arp import Arp
        return Arp
    if module_key == "bounce":
        from .bounce import Bounce
        return Bounce
    if module_key == "chord":
        from .chord import Chord
        return Chord
    if module_key == "delay":
        from .delay import Delay
        return Delay
    if module_key == "gate":
        from .gate import Gate
        return Gate
    if module_key == "latch":
        from .latch import Latch
        return Latch
    if module_key == "pick":
        from .pick import Pick
        return Pick
    if module_key == "quantize":
        from .quantize import Quantize
        return Quantize
    if module_key == "randomize":
        from .randomize import Randomize
        return Randomize
    if module_key == "strum":
        from .strum import Strum
        return Strum
    if module_key == "transpose":
        from .transpose import Transpose
        return Transpose
    if module_key == "wander":
        from .wander import Wander
        return Wander
    return None


def unload_module_class(module_key):
    import gc
    import sys

    import_name = MODULE_IMPORT_NAMES.get(module_key)
    if import_name is None:
        return

    package_name = __name__.rsplit(".", 1)[0]
    attr_name = import_name[1:]
    package = sys.modules.get(package_name)
    if package is not None and hasattr(package, attr_name):
        delattr(package, attr_name)

    module_name = package_name + import_name
    if module_name in sys.modules:
        del sys.modules[module_name]
        gc.collect()
