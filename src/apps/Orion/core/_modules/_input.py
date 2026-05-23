from ..module import Module
from .. import ui
from .. import parms as Parms
from .. import music as Music
from ..note_array import NoteArray, NoteOnArray, NoteOffArray

PADMAP = [9,10,11,6,7,8,3,4,5,0,1,2]

class Input(Module):
    name = "input"
    label = "Input"
    def __init__(self, module_helper, slot_id, include_musical_parms=True):

        ## Init Attributes ##
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.include_musical_parms = include_musical_parms

        self.velocity = 127

        self.parms = []
        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=True, include_source_parms=False)

        operation_mode_parm = self.get_parm_by_name("operation_mode")
        operation_mode_parm.options = operation_mode_parm.options[:-2]

        ## Color Pixels ##
        self.color_pixels()

    def create_main_parms(self):
        parms = []
        from ...inputmodules._registry import (
            AVAILABLE_MODULES,
            AVAILABLE_MODULE_NAMES,
            AVAILABLE_MODULE_LABELS,
        )

        def switch_module(value):
            module_key = AVAILABLE_MODULE_NAMES[value]
            if module_key == self.name:
                return None
            else:
                self.state.set_chain_module(self.slot_id, AVAILABLE_MODULES[module_key])
                self.module_helper.ui_manager.parameter_section.rebuild_parm_section()
                return module_key

        current_module_index = AVAILABLE_MODULE_NAMES.index(self.name)
        module_picker_parm = Parms.Parm("module_picker", "MODE:", Parms.EnumParmType, current_module_index,
                                        options=AVAILABLE_MODULE_LABELS,
                                        exit_callback_function=switch_module)
        parms.append(module_picker_parm)

        if self.include_musical_parms:
            # Key
            key_parm = Parms.Parm(name="key", label="Key", parm_type=Parms.NoteParmType, default=self.state.key,
                                  edit_callback_function=self.set_key)
            parms.append(key_parm)

            #Scale
            scale_parm = Parms.Parm(name="scale", label="Scale", parm_type=Parms.EnumParmType, default=self.state.scale,
                                    options=Music.SCALENAMES, edit_callback_function=self.set_scale)
            parms.append(scale_parm)
            # Octave
            octave_parm = Parms.Parm(name="octave", label="Octave", parm_type=Parms.IntParmType, default=0,
                                  edit_callback_function=self.set_octave)
            parms.append(octave_parm)

            # Key Offset
            key_offset_parm = Parms.Parm(name="key_offset", label="Key Offset", parm_type=Parms.IntParmType, default=0,
                                     edit_callback_function=self.set_key_offset)
            parms.append(key_offset_parm)

            # Velocity
            velocity_parm = Parms.Parm(name="velocity", label="Velocity", parm_type=Parms.IntParmType, default=127,
                                       minmax = [0,127], edit_callback_function=self.set_velocity)
            parms.append(velocity_parm)

        return parms

    def set_key(self, key_id):
        self.state.key = key_id
        self.module_helper.output_manager.all_notes_off()

    def set_scale(self, scale_id):
        self.state.scale = scale_id
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()

    def set_octave(self, octave_id):
        self.state.octave = octave_id
        self.module_helper.output_manager.all_notes_off()

    def set_key_offset(self, key_offset_id):
        self.state.key_offset = key_offset_id
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()

    def set_velocity(self, velocity):
        self.velocity = velocity

    def color_pixels(self, color_overrides: dict = None):
        color_array = [ui.KEYCOLORBASE]*12
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        # set roots
        for idx, color in enumerate(color_array):
            if (idx + self.state.key_offset) % scale_notes == 0:
                key = PADMAP.index(idx)
                color_array[key] = ui.KEYCOLORROOT

        # set overrides
        if color_overrides:
            for key in color_overrides.keys():
                pad_key = PADMAP.index(int(key))
                color_array[pad_key] = color_overrides[key]

        try:
            self.module_helper.ui_manager.neo_pixels.set_key_colors(color_array)
        except:
            pass

    def convert_note(self, pad_note, scale, scale_notes):
        pad_map = PADMAP
        pad_note = pad_map.index(pad_note)
        pad_note += self.state.key_offset
        degree = pad_note % scale_notes
        octave = pad_note // scale_notes + self.state.octave

        return (
                scale[degree]
                + self.state.key
                + (octave + Music.OCTAVEOFFSET) * 12
        )

    def process(self, note_ons, note_offs):
        return note_ons, note_offs

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        
