from ..module import Module
from .. import parms as Parms
from .. import presets

import gc

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
        self.preset_slot = 0

    def create_main_parms(self):
        parms = []
        preset_slot_parm = Parms.Parm("preset_slot", "PRSLOT", Parms.EnumParmType, self.preset_slot,
                                      options=presets.PRESET_SLOT_OPTIONS,
                                      bind_object=self, bind_attribute="preset_slot")
        parms.append(preset_slot_parm)

        save_preset_parm = Parms.Parm("save_preset", "PRSAVE", Parms.ButtonParmType, 0,
                                      enter_callback_function=self.save_preset)
        parms.append(save_preset_parm)

        load_preset_parm = Parms.Parm("load_preset", "PRLOAD", Parms.ButtonParmType, 0,
                                      enter_callback_function=self.load_preset)
        parms.append(load_preset_parm)

        print_ram_usage_parm = Parms.Parm("print_ram", "RAM", Parms.ButtonParmType, None,
                                          enter_callback_function=print_ram_usage)
        parms.append(print_ram_usage_parm)
        key_brightness_parm = Parms.Parm("key_brightness", "KEYBRI", Parms.FloatParmType, 1,
                                          minmax=(0,1), increment=0.05,
                                          edit_callback_function=self.change_key_brightness)
        parms.append(key_brightness_parm)
        return parms

    def save_preset(self, value):
        self.module_helper.orion.save_current_preset(self.preset_slot)
        return value

    def load_preset(self, value):
        self.module_helper.orion.load_preset_slot(self.preset_slot)
        return value

    def change_key_brightness(self, value):
        self.module_helper.neo_pixels.brightness_multiplier = value
        self.module_helper.neo_pixels.paint_pixels()
        return value
