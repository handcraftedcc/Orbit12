from .arp import Arp
from .transpose import Transpose
from .pick import Pick
from .chord import Chord

AVAILABLE_MODULES = {
    "arp": Arp,
    "transpose": Transpose,
    "pick": Pick,
    "chord": Chord
}