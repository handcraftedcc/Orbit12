from ..core.module import Module
from ..core import parms as Parms
from ..core._modules._input import Input, PADMAP

from ..core import music as Music

FOURBYFOURBOTTOM3ROWSMAPPING = [3,7,11,2,6,10,1,5,9,0,4,8]
FOURBYFOURBOTTOM3ROWSMAPPINGFLIPPED = [0,4,8,1,5,9,2,6,10,3,7,11]


class Drum(Input):
    name = "drum"
    label = "Drum"
    def __init__(self, state, slot_id):
        parms = []
        super().__init__(state, slot_id, include_musical_parms=False)

        layout_options = ("InOrder","4x4Left12", "4x4Right12", "4x4Bott3Row","4x4Bott3RowFli")
        self.layout = 0
        # Layout Mode
        layout_mode_parm = Parms.Parm(name="layout", label="Layout", parm_type=Parms.EnumParmType, default=0, options=layout_options,
                                     edit_callback_function=self.set_layout)
        self.parms.append(layout_mode_parm)

        # Key Offset
        key_offset_parm = Parms.Parm(name="key_offset", label="Key Offset", parm_type=Parms.IntParmType, default=0,
                                     edit_callback_function=self.set_key_offset)
        self.parms.append(key_offset_parm)

        # Velocity
        velocity_parm = Parms.Parm(name="velocity", label="Velocity", parm_type=Parms.IntParmType, default=127,
                                   minmax=[0, 127], edit_callback_function=self.set_velocity)
        self.parms.append(velocity_parm)

        self.velocity = 127
        self.state.key = 0
        self.state.scale = 0
        self.state.octave = 0
        self.state.key_offset = 0

    def set_layout(self, layout_id):
        self.layout = layout_id

    def convert_note(self, pad_note, scale, scale_notes):
        pad_note = PADMAP.index(pad_note)
        offset = 0
        if self.layout == 1: #4x4 left 12
            offset = pad_note // 3
            pad_note += offset
        if self.layout == 2:  #4x4 right 12
            offset = pad_note // 3 + 1
            pad_note += offset
        if self.layout == 3: #4x4 Bottom 3 Rows Sideways
            pad_note = FOURBYFOURBOTTOM3ROWSMAPPING[pad_note]
        if self.layout == 4: #4x4 Bottom 3 Rows Sideways
            pad_note = FOURBYFOURBOTTOM3ROWSMAPPINGFLIPPED[pad_note]



        return pad_note+Music.DRUMBASENOTE+self.state.key_offset


    def process(self, note_ons, note_offs, velocities):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.velocities_out.clear()

        for pad_note in note_ons:
            self.note_ons_out.append(self.convert_note(pad_note, None, None))
            self.velocities_out.append(self.velocity)

        for pad_note in note_offs:
            self.note_offs_out.append(self.convert_note(pad_note, None, None))

        return self.note_ons_out, self.note_offs_out, self.velocities_out
        
