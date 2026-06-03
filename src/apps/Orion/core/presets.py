import gc

from . import parms as Parms


file_open = open

PRESET_DIR = "/userdata/Orbit12Orion/presets"
PRESET_PREFIX = "preset"
PRESET_EXT = ".orp"
PRESET_COUNT = 10
PRESET_SLOT_OPTIONS = (
    "P1",
    "P2",
    "P3",
    "P4",
    "P5",
    "P6",
    "P7",
    "P8",
    "P9",
    "P10",
)
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
    "preset_slot",
    "load_preset",
)


class Presets:
    def __init__(self):
        pass


def preset_file_name(slot):
    return PRESET_PREFIX + str(slot + 1) + PRESET_EXT


def preset_filename(slot, base_dir=PRESET_DIR):
    filename = preset_file_name(slot)
    if base_dir.endswith("/"):
        return base_dir + filename
    return base_dir + "/" + filename


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


def save_preset(state, slot=0, base_dir=PRESET_DIR):
    filename = preset_file_name(slot)
    try:
        with file_open(preset_filename(slot, base_dir), "w") as file:
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


def load_preset(orion, slot=0, base_dir=PRESET_DIR):
    state = orion.state
    current_module = None

    try:
        file = file_open(preset_filename(slot, base_dir), "r")
    except OSError:
        return None

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
                    gc.collect()
                except MemoryError:
                    gc.collect()
                    return False
                if current_module is None:
                    return False

            elif record_type == "P" and len(parts) >= 3 and current_module is not None:
                apply_parm_value(current_module, parts[1], parts[2])

    return True
