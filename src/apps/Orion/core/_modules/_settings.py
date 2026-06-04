from ..module import Module
from .. import parms as Parms

import gc

SCENE_OPTIONS = (
    "01",
    "02",
    "03",
    "04",
    "05",
    "06",
    "07",
    "08",
    "09",
    "10",
)

RESET_OPTIONS = (">", "YES")

def print_ram_usage(value):
    gc.collect()
    print("allocated:",gc.mem_alloc())
    print("free:",gc.mem_free())

class Settings(Module):
    name = "settings"
    label = "STTNGS"
    def __init__(self, module_helper, slot_id, macropad = None):
        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False, include_source_parms=False)
        self.macropad = self.state.macropad
        self.reset_scene_parm = None

    def create_main_parms(self):
        parms = []
        active_scene_parm = Parms.Parm("active_scene", "SCENE", Parms.EnumParmType, self.state.active_scene,
                                       options=SCENE_OPTIONS,
                                       exit_callback_function=self.switch_scene)
        parms.append(active_scene_parm)

        save_scene_parm = Parms.Parm("save_scene", "SAVSCN", Parms.ButtonParmType, 0,
                                     enter_callback_function=self.save_scene)
        parms.append(save_scene_parm)

        reset_scene_parm = Parms.Parm("reset_scene", "RSTSCN", Parms.EnumParmType, 0,
                                      options=RESET_OPTIONS,
                                      enter_callback_function=self.arm_reset_scene,
                                      exit_callback_function=self.reset_scene)
        self.reset_scene_parm = reset_scene_parm
        parms.append(reset_scene_parm)

        print_ram_usage_parm = Parms.Parm("print_ram", "RAM", Parms.ButtonParmType, None,
                                          enter_callback_function=print_ram_usage)
        parms.append(print_ram_usage_parm)
        key_brightness_parm = Parms.Parm("key_brightness", "KEYBRI", Parms.FloatParmType,
                                          self.module_helper.neo_pixels.brightness_multiplier,
                                          minmax=(0,1), increment=0.05,
                                          edit_callback_function=self.change_key_brightness)
        parms.append(key_brightness_parm)
        return parms

    def save_scene(self, value):
        self.module_helper.orion.save_scene()
        return value

    def switch_scene(self, value):
        self.module_helper.orion.switch_scene(value)
        return value

    def queue_parm_rebuild(self):
        orion = self.module_helper.orion
        orion.add_to_ui_queue(orion.ui_manager.parameter_section.rebuild_parm_section)

    def arm_reset_scene(self, value):
        if self.reset_scene_parm is not None:
            self.reset_scene_parm.display_value = "SUR?"
            self.queue_parm_rebuild()
        return value

    def reset_scene(self, value):
        if value == 1:
            self.module_helper.orion.reset_scene_and_reload()
            return value

        if self.reset_scene_parm is not None:
            self.reset_scene_parm.set_value(0)
            self.queue_parm_rebuild()
        return value

    def change_key_brightness(self, value):
        self.module_helper.neo_pixels.brightness_multiplier = value
        self.module_helper.neo_pixels.paint_pixels()
        self.module_helper.orion.save_user_settings()
        return value
