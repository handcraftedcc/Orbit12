from ..module import Module
from .. import parms as Parms
from .. import music as Music

PADMAP = [9,10,11,6,7,8,3,4,5,0,1,2]

class Input(Module):
    name = "input"
    label = "Input"
    def __init__(self, module_helper, slot_id, include_musical_parms=True):
        from ...inputmodules._registry import (
            AVAILABLE_MODULES,
            AVAILABLE_MODULE_NAMES,
            AVAILABLE_MODULE_LABELS,
        )

        parms = []
        super().__init__(module_helper, slot_id, parms, include_default_parms=False)

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
        self.parms.append(module_picker_parm)

        if include_musical_parms:
            # Key
            key_parm = Parms.Parm(name="key", label="Key", parm_type=Parms.NoteParmType, default=self.state.key,
                                  edit_callback_function=self.set_key)
            self.parms.append(key_parm)

            #Scale
            scale_parm = Parms.Parm(name="scale", label="Scale", parm_type=Parms.EnumParmType, default=self.state.scale,
                                    options=Music.SCALENAMES, edit_callback_function=self.set_scale)
            self.parms.append(scale_parm)
            # Octave
            octave_parm = Parms.Parm(name="octave", label="Octave", parm_type=Parms.IntParmType, default=0,
                                  edit_callback_function=self.set_octave)
            self.parms.append(octave_parm)

            # Key Offset
            key_offset_parm = Parms.Parm(name="key_offset", label="Key Offset", parm_type=Parms.IntParmType, default=0,
                                     edit_callback_function=self.set_key_offset)
            self.parms.append(key_offset_parm)

            # Velocity
            velocity_parm = Parms.Parm(name="velocity", label="Velocity", parm_type=Parms.IntParmType, default=127,
                                       minmax = [0,127], edit_callback_function=self.set_velocity)
            self.parms.append(velocity_parm)

            self.velocity = 127

        ## Init Attributes ##
        self.note_ons_out = []
        self.note_offs_out = []
        self.velocities_out = []

    def set_key(self, key_id):
        self.state.key = key_id
        self.module_helper.output_manager.all_notes_off()

    def set_scale(self, scale_id):
        self.state.scale = scale_id
        self.module_helper.output_manager.all_notes_off()

    def set_octave(self, octave_id):
        self.state.octave = octave_id
        self.module_helper.output_manager.all_notes_off()

    def set_key_offset(self, key_offset_id):
        self.state.key_offset = key_offset_id
        self.module_helper.output_manager.all_notes_off()

    def set_velocity(self, velocity):
        self.velocity = velocity

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

    def process(self, note_ons, note_offs, velocities):
        return note_ons, note_offs, velocities

        
