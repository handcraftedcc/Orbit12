from ..core import parms as Parms
from ..core import neo_pixels
from ..core._modules._input import Input, PADMAP
from ..core.note_array import NoteOnArray, NoteOffArray

from ..core import music as Music

FOURBYFOURBOTTOM3ROWSMAPPING = [3,7,11,2,6,10,1,5,9,0,4,8]
FOURBYFOURBOTTOM3ROWSMAPPINGFLIPPED = [0,4,8,1,5,9,2,6,10,3,7,11]


class Drum(Input):
    name = "drum"
    label = "DRUM"
    def __init__(self, module_helper, slot_id):
        self.layout = 0
        self.velocity = 127
        super().__init__(module_helper, slot_id, include_musical_parms=False)

        self.state.octave = 0
        self.state.key_offset = 0

        self.ui_manager.header_footer.update_header_key_info(alt_text = "drums")

    def create_main_parms(self):
        parms = super().create_main_parms()
        layout_options = ("ORD","L12", "R12", "B3R","B3F")
        # Layout Mode
        layout_mode_parm = Parms.Parm(name="layout", label="LAY", parm_type=Parms.EnumParmType, default=0, options=layout_options,
                                     edit_callback_function=self.set_layout)
        parms.append(layout_mode_parm)

        # Key Offset
        key_offset_parm = Parms.Parm(name="key_offset", label="KEY OFS", parm_type=Parms.IntParmType, default=0,
                                     edit_callback_function=self.set_key_offset)
        parms.append(key_offset_parm)

        # Velocity
        velocity_parm = Parms.Parm(name="velocity", label="VEL", parm_type=Parms.IntParmType, default=127,
                                   minmax=[0, 127], edit_callback_function=self.set_velocity)
        parms.append(velocity_parm)
        return parms

    def set_layout(self, layout_id):
        self.layout = layout_id
        self.module_helper.output_manager.all_notes_off()

    def color_pixels(self, color_overrides: dict = None):
        color_array = [neo_pixels.KEYCOLORDRUMS]*12

        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

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


    def process(self, note_ons:NoteOnArray, note_offs:NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            self.note_ons_out.append_value(self.convert_note(pad_note, None, None), velocity = self.velocity)

        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            self.note_offs_out.append_value(self.convert_note(pad_note, None, None))

        return self.note_ons_out, self.note_offs_out
        
