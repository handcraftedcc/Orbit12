from ..module import Module
from .. import neo_pixels
from .. import parms as Parms
from .. import music as Music
from ..note_array import NoteArray, NoteOnArray, NoteOffArray

PADMAP = (9,10,11,6,7,8,3,4,5,0,1,2)
INPUT_OPERATION_OPTIONS = ("NEXT", "OUT+N")

class Input(Module):
    name = "input"
    label = "IN"
    def __init__(self, module_helper, slot_id, include_musical_parms=True):

        ## Init Attributes ##
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.include_musical_parms = include_musical_parms

        self.velocity = 127

        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=True, include_source_parms=False)

        ## Color Pixels ##
        self.color_pixels()

    def create_main_parms(self):
        parms = []

        if self.include_musical_parms:
            # Key
            key_parm = Parms.Parm(name="key", label="KEY", parm_type=Parms.NoteParmType, default=self.state.key,
                                  edit_callback_function=self.set_key)
            parms.append(key_parm)

            #Scale
            scale_parm = Parms.Parm(name="scale", label="SCL", parm_type=Parms.EnumParmType, default=self.state.scale,
                                    options=Music.SCALENAMES, edit_callback_function=self.set_scale)
            parms.append(scale_parm)
            # Octave
            octave_parm = Parms.Parm(name="octave", label="OCT", parm_type=Parms.IntParmType, default=self.state.octave,
                                  edit_callback_function=self.set_octave)
            parms.append(octave_parm)

            # Key Offset
            key_offset_parm = Parms.Parm(name="key_offset", label="PADOFS", parm_type=Parms.IntParmType, default=self.state.key_offset,
                                     edit_callback_function=self.set_key_offset)
            parms.append(key_offset_parm)

            # Velocity
            velocity_parm = Parms.Parm(name="velocity", label="VEL", parm_type=Parms.IntParmType, default=self.velocity,
                                       minmax = (0,127), edit_callback_function=self.set_velocity)
            parms.append(velocity_parm)

        return parms

    def create_out_parms(self):
        parms = super().create_out_parms()
        parms[0].options = INPUT_OPERATION_OPTIONS
        parms[0].display_value = parms[0].get_display_value()
        return parms

    def set_key(self, key_id):
        self.state.key = key_id
        self.module_helper.output_manager.all_notes_off()
        self.module_helper.ui_manager.header_footer.update_header_key_info()

    def set_scale(self, scale_id):
        self.state.scale = scale_id
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()
        self.module_helper.ui_manager.header_footer.update_header_key_info()

    def set_octave(self, octave_id):
        self.state.octave = octave_id
        self.module_helper.output_manager.all_notes_off()
        self.module_helper.ui_manager.header_footer.update_header_key_info()

    def set_key_offset(self, key_offset_id):
        self.state.key_offset = key_offset_id
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()

    def set_velocity(self, velocity):
        self.velocity = velocity

    def color_pixels(self, color_overrides: dict = None):
        color_array = [neo_pixels.KEYCOLORBASE]*12
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        # set roots
        for idx, color in enumerate(color_array):
            if (idx + self.state.key_offset) % scale_notes == 0:
                key = PADMAP.index(idx)
                color_array[key] = neo_pixels.KEYCOLORROOT

        # set overrides
        if color_overrides:
            for key in color_overrides.keys():
                pad_key = PADMAP.index(int(key))
                color_array[pad_key] = color_overrides[key]

        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
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
                + (octave+2) * 12
        )

    def process(self, note_ons, note_offs):
        return note_ons, note_offs

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        
