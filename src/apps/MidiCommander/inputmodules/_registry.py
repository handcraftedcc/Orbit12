from .note import Note
from .drum import Drum

AVAILABLE_MODULES = {
    "note": Note,
    "drum": Drum,
}

AVAILABLE_MODULE_NAMES = list(AVAILABLE_MODULES.keys())
AVAILABLE_MODULE_LABELS = [module.label for module in AVAILABLE_MODULES.values()]