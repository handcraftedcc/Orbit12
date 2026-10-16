from ..core import parms as Parms
from ..core import neo_pixels
from ..core._modules._input import Input, PADMAP
from ..core.note_array import NoteRelationshipArray

from ..core import music as Music

### DRUM LAYOUTS ###

FOURBYFOURBOTTOM3ROWSMAPPING = (3,7,11,2,6,10,1,5,9,0,4,8)
FOURBYFOURBOTTOM3ROWSMAPPINGFLIPPED = (0,4,8,1,5,9,2,6,10,3,7,11)
LAYOUT_OPTIONS = ("ORD", "L12", "R12", "B3R", "B3F")


### DRUM INPUT ###

class Drum(Input):
    name = "drum"
    label = "DRUM"
    def __init__(self, module_helper, slot_id):
        self.layout = 0
        self.base_note = Music.DRUMBASENOTE
        self.velocity = 127
        self.held_note_relationship = NoteRelationshipArray(12)
        super().__init__(module_helper, slot_id, include_musical_parms=False)

        self.state.key_custom_text = "DRUM"

        self.ui_manager.header_footer.update_header_key_info()

    ### PARMS ###

    def create_main_parms(self):
        parms = super().create_main_parms()
        # Layout Mode
        layout_mode_parm = Parms.Parm(name="layout", label="LAY", parm_type=Parms.EnumParmType, default=self.layout, options=LAYOUT_OPTIONS,
                                     edit_callback_function=self.set_layout)
        parms.append(layout_mode_parm)

        base_note_parm = Parms.Parm(
            name="base_note",
            label="BASE",
            parm_type=Parms.NoteParmType,
            default=self.base_note,
            minmax=(0, 127),
            multiple_octaves=True,
            octave_range=(1, 11),
            bind_object=self,
            bind_attribute="base_note",
        )
        parms.append(base_note_parm)

        # Key Offset
        key_offset_parm = Parms.Parm(name="key_offset", label="PADOFS", parm_type=Parms.IntParmType, default=self.state.key_offset,
                                     edit_callback_function=self.set_key_offset)
        parms.append(key_offset_parm)

        # Velocity
        velocity_parm = Parms.Parm(name="velocity", label="VEL", parm_type=Parms.IntParmType, default=self.velocity,
                                   minmax=(0, 127), bind_object=self, bind_attribute="velocity")
        parms.append(velocity_parm)
        return parms

    def set_layout(self, layout_id):
        self.layout = layout_id
        self.module_helper.output_manager.all_notes_off()

    ### KEY COLORS ###

    def color_pixels(self, color_overrides = None):
        color_array = [neo_pixels.KEYCOLORDRUMS]*12

        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

    ### NOTE CONVERSION ###

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


        return pad_note + self.base_note + self.state.key_offset


    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            off_notes = self.held_note_relationship.remove_note_all(pad_note)
            for j in range(off_notes.return_length):
                note = off_notes.return_notes[j]
                has_out_note, _ = self.held_note_relationship.has_out_note(note)
                if not has_out_note:
                    self.note_offs_out.append_value(note)

        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            note = self.convert_note(pad_note, None, None)
            self.note_ons_out.append_value(note, velocity = self.velocity)
            self.held_note_relationship.add_note(pad_note, note)

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        super().stop()
        self.held_note_relationship.clear()

    def remove(self):
        super().remove()
        self.held_note_relationship = None
        
