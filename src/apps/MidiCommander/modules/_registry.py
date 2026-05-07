from ..core._modules._empty import Empty
from .arp import Arp
from .transpose import Transpose
from .pick import Pick
from .chord import Chord

AVAILABLE_MODULES = {
    "empty": Empty,
    "arp": Arp,
    "transpose": Transpose,
    "pick": Pick,
    "chord": Chord
}

AVAILABLE_MODULE_NAMES = list(AVAILABLE_MODULES.keys())
AVAILABLE_MODULE_LABELS = [module.label for module in AVAILABLE_MODULES.values()]