MODULE_SPECS = (
    ("note", "NOTE"),
    ("chords", "CHRD"),
    ("drum", "DRUM"),
)

AVAILABLE_MODULE_NAMES = [spec[0] for spec in MODULE_SPECS]
AVAILABLE_MODULE_LABELS = [spec[1] for spec in MODULE_SPECS]
AVAILABLE_MODULE_HELP_TEXTS = ()

MODULE_IMPORT_NAMES = {
    "note": ".note",
    "chords": ".chords",
    "drum": ".drum",
}


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
