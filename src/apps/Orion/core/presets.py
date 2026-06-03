import os
import gc

from . import parms as Parms


file_open = open

PRESET_DIR = "/userdata/Orbit12Orion/presets"
PRESET_PREFIX = "preset"
PRESET_EXT = ".orp"
HEADER = "ORP1"

STATE_ATTRS = (
    "key",
    "scale",
    "octave",
    "key_offset",
    "bpm",
    "swing",
    "transport_mode",
)

PRESET_UI_PARMS = (
    "save_preset",
    "preset_file",
    "load_preset",
)


class Presets:
    def __init__(self):
        pass


def join_path(base_dir, filename):
    if base_dir.endswith("/"):
        return base_dir + filename
    return base_dir + "/" + filename


def ensure_dir(path):
    current = ""
    for part in path.split("/"):
        if part == "":
            if current == "":
                current = "/"
            continue

        if current == "" or current == "/":
            current = current + part
        else:
            current = current + "/" + part

        try:
            os.mkdir(current)
        except OSError:
            pass


def is_preset_file(filename):
    return (
        filename.startswith(PRESET_PREFIX)
        and filename.endswith(PRESET_EXT)
    )


def list_preset_files(base_dir=PRESET_DIR):
    try:
        files = os.listdir(base_dir)
    except OSError:
        return []

    presets = []
    for filename in files:
        if is_preset_file(filename):
            presets.append(filename)
    presets.sort()
    return presets


def next_preset_filename(base_dir=PRESET_DIR):
    files = list_preset_files(base_dir)
    index = 1
    while True:
        filename = PRESET_PREFIX + str(index) + PRESET_EXT
        if filename not in files:
            return filename
        index += 1


def value_to_text(value):
    if value is True:
        return "1"
    if value is False:
        return "0"
    return str(value)


def should_save_parm(parm):
    if parm.name in PRESET_UI_PARMS:
        return False
    return parm.type is not Parms.ButtonParmType


def write_state(file, state):
    for attr in STATE_ATTRS:
        file.write("S,")
        file.write(attr)
        file.write(",")
        file.write(value_to_text(getattr(state, attr)))
        file.write("\n")


def write_module(file, slot_id, module):
    if module is None:
        return

    version = getattr(module, "version", 1)
    file.write("M,")
    file.write(str(slot_id))
    file.write(",")
    file.write(module.name)
    file.write(",")
    file.write(str(version))
    file.write("\n")

    had_parms = module.parms is not None
    parms = module.get_parms()
    for parm in parms:
        if should_save_parm(parm):
            file.write("P,")
            file.write(parm.name)
            file.write(",")
            file.write(value_to_text(parm.value))
            file.write("\n")

    if not had_parms:
        module.release_parms()


def save_preset(state, filename=None, base_dir=PRESET_DIR):
    ensure_dir(base_dir)
    if filename is None:
        filename = next_preset_filename(base_dir)

    try:
        with file_open(join_path(base_dir, filename), "w") as file:
            file.write(HEADER)
            file.write("\n")
            write_state(file, state)

            for slot_id in range(len(state.chain_modules)):
                write_module(file, slot_id, state.chain_modules[slot_id])

            file.write("END\n")
    except OSError:
        return None

    return filename


def parse_state_value(attr, text):
    if attr == "swing":
        return float(text)
    return int(text)


def parse_parm_value(parm, text):
    parm_type = parm.type
    if parm_type is Parms.FloatParmType or parm_type is Parms.PercentParmType:
        return float(text)
    if (
        parm_type is Parms.IntParmType
        or parm_type is Parms.BooleanParmType
        or parm_type is Parms.EnumParmType
        or parm_type is Parms.PatternParmType
        or parm_type is Parms.RateParmType
        or parm_type is Parms.NoteParmType
    ):
        return int(float(text))
    return text


def apply_parm_value(module, parm_name, text):
    parm = module.get_parm_by_name(parm_name)
    if parm is None:
        return False

    value = parse_parm_value(parm, text)
    parm.set_value(value)

    if parm.edit_callback_function is not None:
        parm.edit_callback_function(parm.value)
    if parm.exit_callback_function is not None:
        parm.exit_callback_function(parm.value)

    return True


def load_preset(orion, filename, base_dir=PRESET_DIR):
    state = orion.state
    current_module = None

    try:
        file = file_open(join_path(base_dir, filename), "r")
    except OSError:
        return False

    with file:
        header = file.readline().strip()
        if header != HEADER:
            return False

        while True:
            line = file.readline()
            if not line:
                break

            line = line.strip()
            if line == "" or line == "END":
                break

            parts = line.split(",")
            record_type = parts[0]

            if record_type == "S" and len(parts) >= 3:
                attr = parts[1]
                if attr in STATE_ATTRS:
                    setattr(state, attr, parse_state_value(attr, parts[2]))

            elif record_type == "M" and len(parts) >= 4:
                try:
                    current_module = orion.load_preset_module(int(parts[1]), parts[2])
                except MemoryError:
                    gc.collect()
                    return False
                if current_module is None:
                    return False

            elif record_type == "P" and len(parts) >= 3 and current_module is not None:
                apply_parm_value(current_module, parts[1], parts[2])

    return True
