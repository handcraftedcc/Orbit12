import gc

from . import parms as Parms


### SCENE FORMAT ###

SCENE_DIR = "/userdata/Orbit12Orion/scenes"
SCENE_PREFIX = "Scene"
ACTIVE_SCENE_FILE = "_activescene"
SCENE_COUNT = 10
HEADER = "SCN1"
ATTR_SEPARATOR = "|"

STATE_ATTRS = (
    "key",
    "scale",
    "octave",
    "key_offset",
    "bpm",
    "swing",
    "transport_mode",
)

SCENE_UI_PARMS = (
    "active_scene",
    "save_scene",
    "copy_scene",
    "reset_scene",
    "key_brightness",
)


### FILE HELPERS ###

def clamp_scene(scene):
    if scene < 0:
        return 0
    if scene >= SCENE_COUNT:
        return SCENE_COUNT - 1
    return scene


def scene_file_name(scene):
    return SCENE_PREFIX + "{:02d}".format(scene + 1)


def scene_filename(scene, base_dir=SCENE_DIR):
    filename = scene_file_name(clamp_scene(scene))
    if base_dir.endswith("/"):
        return base_dir + filename
    return base_dir + "/" + filename


def active_scene_filename(base_dir=SCENE_DIR):
    if base_dir.endswith("/"):
        return base_dir + ACTIVE_SCENE_FILE
    return base_dir + "/" + ACTIVE_SCENE_FILE


### ACTIVE SCENE ###

def read_active_scene(base_dir=SCENE_DIR, default_scene=0):
    default_scene = clamp_scene(default_scene)
    try:
        with open(active_scene_filename(base_dir), "r") as file:
            scene = int(file.readline().strip()) - 1
            return clamp_scene(scene)
    except (OSError, ValueError):
        write_active_scene(default_scene, base_dir)
        return default_scene


def write_active_scene(scene, base_dir=SCENE_DIR):
    scene = clamp_scene(scene)
    try:
        with open(active_scene_filename(base_dir), "w") as file:
            file.write(str(scene + 1))
            file.write("\n")
    except OSError:
        return False
    return True


### SAVE HELPERS ###

def value_to_text(value):
    if value is True:
        return "1"
    if value is False:
        return "0"
    return str(value)


def attr_value_to_text(value):
    if isinstance(value, (list, tuple, bytearray)):
        return ATTR_SEPARATOR.join(value_to_text(item) for item in value)
    return value_to_text(value)


def parse_scalar_like(current_value, text):
    if isinstance(current_value, bool):
        return text == "1" or text == "True"
    if isinstance(current_value, float):
        return float(text)
    if isinstance(current_value, int):
        return int(float(text))
    return text


def parse_attr_value(current_value, text):
    if isinstance(current_value, bytearray):
        if text == "":
            return bytearray()
        return bytearray(int(float(part)) for part in text.split(ATTR_SEPARATOR))

    if isinstance(current_value, (list, tuple)):
        if text == "":
            values = []
        else:
            parts = text.split(ATTR_SEPARATOR)
            sample = current_value[0] if len(current_value) else 0
            values = [parse_scalar_like(sample, part) for part in parts]
        if isinstance(current_value, tuple):
            return tuple(values)
        return values

    return parse_scalar_like(current_value, text)


def should_save_parm(parm):
    if parm.name in SCENE_UI_PARMS:
        return False
    return parm.type is not Parms.ButtonParmType


def should_save_attr(module, attr):
    return hasattr(module, attr)


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

    file.write("M,")
    file.write(str(slot_id))
    file.write(",")
    file.write(module.name)
    file.write(",")
    file.write(str(getattr(module, "version", 1)))
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

    for attr in getattr(module, "save_attrs", ()):
        if should_save_attr(module, attr):
            file.write("A,")
            file.write(attr)
            file.write(",")
            file.write(attr_value_to_text(getattr(module, attr)))
            file.write("\n")

    if not had_parms:
        module.release_parms()


def remap_scene_module_record(slot_id, module_key):
    if slot_id == 0 and module_key != "set":
        return 1, module_key
    if slot_id == 1 and module_key == "transport":
        return 0, "set"
    return slot_id, module_key


def save_scene(state, scene=None, base_dir=SCENE_DIR):
    if scene is None:
        scene = state.active_scene
    scene = clamp_scene(scene)
    filename = scene_file_name(scene)
    try:
        with open(scene_filename(scene, base_dir), "w") as file:
            file.write(HEADER)
            file.write("\n")
            write_state(file, state)

            for slot_id in range(len(state.chain_modules)):
                write_module(file, slot_id, state.chain_modules[slot_id])

            file.write("END\n")
    except OSError:
        return None

    return filename


### LOAD HELPERS ###

def parse_parm_value(parm, text):
    parm_type = parm.type
    if parm_type is Parms.FloatParmType or parm_type is Parms.PercentParmType:
        return float(text)
    if parm_type is Parms.StringParmType:
        return text
    return int(float(text))


def restore_module_attr(module, attr, text):
    save_attrs = getattr(module, "save_attrs", ())
    if attr not in save_attrs or not hasattr(module, attr):
        return

    current_value = getattr(module, attr)
    restored_value = parse_attr_value(current_value, text)

    if isinstance(current_value, list) and isinstance(restored_value, list):
        current_value[:] = restored_value
    elif isinstance(current_value, bytearray) and isinstance(restored_value, bytearray):
        current_value[:] = restored_value
    else:
        setattr(module, attr, restored_value)


def load_scene(orion, scene=None, base_dir=SCENE_DIR):
    state = orion.state
    if scene is None:
        scene = state.active_scene
    scene = clamp_scene(scene)
    current_module = None

    try:
        file = open(scene_filename(scene, base_dir), "r")
    except OSError:
        return None

    with file:
        header = file.readline().strip()
        if header != HEADER:
            return False

        # Scene records are ordered: state values, module headers, then module parms.
        for line in file:
            line = line.strip()
            if line == "" or line == "END":
                break

            parts = line.split(",")
            record_type = parts[0]

            if record_type == "S" and len(parts) >= 3:
                # Restore global musical and transport state.
                attr = parts[1]
                if attr in STATE_ATTRS:
                    if attr == "swing":
                        setattr(state, attr, float(parts[2]))
                    else:
                        setattr(state, attr, int(parts[2]))

            elif record_type == "M" and len(parts) >= 4:
                # Load the current module so following parm records target it.
                try:
                    slot_id, module_key = remap_scene_module_record(int(parts[1]), parts[2])
                    current_module = orion.load_scene_module(slot_id, module_key)
                    gc.collect()
                except MemoryError:
                    gc.collect()
                    return False
                if current_module is None:
                    return False

            elif record_type == "P" and len(parts) >= 3 and current_module is not None:
                # Apply saved parm values through normal callbacks.
                if parts[1] in SCENE_UI_PARMS:
                    continue
                parm = current_module.get_parm_by_name(parts[1])
                if parm is not None:
                    parm.set_value(parse_parm_value(parm, parts[2]))

                    if parm.edit_callback_function is not None:
                        parm.edit_callback_function(parm.value)
                    if parm.exit_callback_function is not None:
                        parm.exit_callback_function(parm.value)

            elif record_type == "A" and len(parts) >= 3 and current_module is not None:
                restore_module_attr(current_module, parts[1], parts[2])

    return True
