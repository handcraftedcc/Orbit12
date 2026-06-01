MODULE_SPECS = (
    ("note", "NOTE", "IN KEY NOTE LYT"),
    ("chords", "CHRD", "CHORD GENERATOR"),
    ("drum", "DRUM", "DRUM PAD LAYOUT"),
)

AVAILABLE_MODULE_NAMES = [spec[0] for spec in MODULE_SPECS]
AVAILABLE_MODULE_LABELS = [spec[1] for spec in MODULE_SPECS]
AVAILABLE_MODULE_HELP_TEXTS = [spec[2] for spec in MODULE_SPECS]


def get_module_class(module_key):
    import gc
    gc.collect()

    if module_key == "note":
        from .note import Note
        return Note
    if module_key == "chords":
        from .chords import Chords
        return Chords
    if module_key == "drum":
        from .drum import Drum
        return Drum
    return None
