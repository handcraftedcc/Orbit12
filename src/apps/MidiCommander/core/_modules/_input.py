from ..module import Module
from .. import parms as Parms
from .. import music as Scales
import gc


def print_ram_usage():
    gc.collect()
    print(gc.mem_alloc())
    print(gc.mem_free())


class Input(Module):
    name = "input"
    label = "Input"
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms, include_default_parms=False)

            print_ram_usage_parm = Parms.Parm("printRam","Print Ram", Parms.ButtonParmType, None,
                                              enter_callback_function=print_ram_usage)
            self.parms.append(print_ram_usage_parm)

            # Root
            key_parm = Parms.Parm(name="key", label="Key", parm_type=Parms.NoteParmType, default=state.key,
                                  exit_callback_function=self.set_key)
            self.parms.append(key_parm)

            #Scale
            scale_parm = Parms.Parm(name="scale", label="Scale", parm_type=Parms.EnumParmType, default=state.scale,
                                    options=Scales.SCALENAMES, exit_callback_function=self.set_scale)
            self.parms.append(scale_parm)
            # Octave
            octave_parm = Parms.Parm(name="octave", label="Octave", parm_type=Parms.IntParmType, default=0,
                                  exit_callback_function=self.set_octave)
            self.parms.append(octave_parm)

            # Key Offset
            key_offset_parm = Parms.Parm(name="key_offset", label="Key Offset", parm_type=Parms.IntParmType, default=0,
                                     exit_callback_function=self.set_key_offset)
            self.parms.append(key_offset_parm)


    def set_key(self, key_id):
        self.state.key = key_id

    def set_scale(self, scale_id):
        self.state.scale = Scales.SCALENAMES[scale_id]

    def set_octave(self, octave_id):
        self.state.octave = octave_id

    def set_key_offset(self, key_offset_id):
        self.state.key_offset = key_offset_id




        
