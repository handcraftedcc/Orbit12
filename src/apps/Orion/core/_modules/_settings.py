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
        self.preset_files = []
        self.preset_file = 0
        self.preset_file_options = ("NONE",)
        self.refresh_preset_files()

    def create_main_parms(self):
        parms = []
        save_preset_parm = Parms.Parm("save_preset", "SAVE", Parms.ButtonParmType, 0,
                                      enter_callback_function=self.save_preset)
        parms.append(save_preset_parm)

        preset_file_parm = Parms.Parm("preset_file", "LDFILE", Parms.EnumParmType, self.preset_file,
                                      options=self.preset_file_options,
                                      edit_callback_function=self.set_preset_file,
                                      help_text=self.get_preset_file_help_text())
        parms.append(preset_file_parm)

        load_preset_parm = Parms.Parm("load_preset", "LOAD", Parms.ButtonParmType, 0,
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

    def preset_label(self, filename):
        if filename.startswith(presets.PRESET_PREFIX) and filename.endswith(presets.PRESET_EXT):
            number = filename[len(presets.PRESET_PREFIX):-len(presets.PRESET_EXT)]
            return "PST" + number
        return filename

    def get_preset_file_help_text(self):
        if not self.preset_files:
            return "NO PRESETS"
        return self.preset_files[self.preset_file]

    def refresh_preset_files(self, selected_filename=None):
        self.preset_files = presets.list_preset_files()
        if self.preset_files:
            if selected_filename in self.preset_files:
                self.preset_file = self.preset_files.index(selected_filename)
            elif self.preset_file >= len(self.preset_files):
                self.preset_file = len(self.preset_files) - 1
            labels = []
            for filename in self.preset_files:
                labels.append(self.preset_label(filename))
            self.preset_file_options = tuple(labels)
        else:
            self.preset_file = 0
            self.preset_file_options = ("NONE",)

        if self.parms is not None:
            parm = self.get_parm_by_name("preset_file")
            if parm is not None:
                parm.options = self.preset_file_options
                parm.set_value(self.preset_file)
                parm.help_text = self.get_preset_file_help_text()

    def set_preset_file(self, value):
        self.preset_file = value
        parm = self.get_parm_by_name("preset_file")
        if parm is not None:
            parm.help_text = self.get_preset_file_help_text()
        return value

    def save_preset(self, value):
        filename = self.module_helper.orion.save_current_preset()
        self.refresh_preset_files(filename)
        return value

    def load_preset(self, value):
        if not self.preset_files:
            self.module_helper.orion.update_help_text("NO PRESET")
            return value
        self.module_helper.orion.load_preset(self.preset_files[self.preset_file])
        return value

    def change_key_brightness(self, value):
        self.module_helper.neo_pixels.brightness_multiplier = value
        self.module_helper.neo_pixels.paint_pixels()
        return value
