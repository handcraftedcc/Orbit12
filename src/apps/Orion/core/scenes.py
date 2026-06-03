import gc

from . import parms as Parms


SCENE_DIR = "/userdata/Orbit12Orion/scenes"
SCENE_PREFIX = "Scene"
ACTIVE_SCENE_FILE = "_activescene"
SCENE_COUNT = 10
HEADER = "SCN1"

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
    "reset_scene",
    "key_brightness",
)


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


def value_to_text(value):
    if value is True:
        return "1"
    if value is False:
        return "0"
    return str(value)


def should_save_parm(parm):
    if parm.name in SCENE_UI_PARMS:
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

    if not had_parms:
        module.release_parms()


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


def parse_parm_value(parm, text):
    parm_type = parm.type
    if parm_type is Parms.FloatParmType or parm_type is Parms.PercentParmType:
        return float(text)
    if parm_type is Parms.StringParmType:
        return text
    return int(float(text))


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

        for line in file:
            line = line.strip()
            if line == "" or line == "END":
                break

            parts = line.split(",")
            record_type = parts[0]

            if record_type == "S" and len(parts) >= 3:
                attr = parts[1]
                if attr in STATE_ATTRS:
                    if attr == "swing":
                        setattr(state, attr, float(parts[2]))
                    else:
                        setattr(state, attr, int(parts[2]))

            elif record_type == "M" and len(parts) >= 4:
                try:
                    current_module = orion.load_scene_module(int(parts[1]), parts[2])
                    gc.collect()
                except MemoryError:
                    gc.collect()
                    return False
                if current_module is None:
                    return False

            elif record_type == "P" and len(parts) >= 3 and current_module is not None:
                if parts[1] in SCENE_UI_PARMS:
                    continue
                parm = current_module.get_parm_by_name(parts[1])
                if parm is not None:
                    parm.set_value(parse_parm_value(parm, parts[2]))

                    if parm.edit_callback_function is not None:
                        parm.edit_callback_function(parm.value)
                    if parm.exit_callback_function is not None:
                        parm.exit_callback_function(parm.value)

    return True
