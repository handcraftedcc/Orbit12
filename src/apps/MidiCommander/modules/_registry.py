from .arp import Arp
from .transpose import Transpose
from .pick import Pick
from .chord import Chord

AVAILABLE_MODULES = {
    "Arp": Arp,
    "Transpose": Transpose,
    "Pick": Pick,
    "Chord": Chord
}