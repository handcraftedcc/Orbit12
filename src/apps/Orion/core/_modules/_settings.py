from ..module import Module
from .. import parms as Parms

import gc

def print_ram_usage(value):
    gc.collect()
    print("allocated:",gc.mem_alloc())
    print("free:",gc.mem_free())

class Settings(Module):
    name = "settings"
    label = "STTNGS"
    help_text = "GLOBAL SETTINGS"
    def __init__(self, module_helper, slot_id, macropad = None):
        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False, include_source_parms=False)
        self.macropad = self.state.macropad

    def create_main_parms(self):
        parms = []
        print_ram_usage_parm = Parms.Parm("print_ram", "RAM", Parms.ButtonParmType, None,
                                          help_text="PRINT RAM FREE",
                                          enter_callback_function=print_ram_usage)
        parms.append(print_ram_usage_parm)
        key_brightness_parm = Parms.Parm("key_brightness", "KEYBRI", Parms.FloatParmType, 1,
                                          minmax=(0,1), increment=0.05,
                                          help_text="KEY BRIGHTNESS",
                                          edit_callback_function=self.change_key_brightness)
        parms.append(key_brightness_parm)
        return parms

    def change_key_brightness(self, value):
        self.module_helper.neo_pixels.brightness_multiplier = value
        self.module_helper.neo_pixels.paint_pixels()
        return value
