from ..core import neo_pixels
from ..core import parms as Parms
from ..core._modules._input import Input, PADMAP

from ..core import music as Music


### NOTE INPUT ###

NOTE_LAYOUT_KEY = 0
NOTE_LAYOUT_CHROMATIC = 1
NOTE_LAYOUT_OPTIONS = ("KEY", "CHRM")
OFF_SCALE_COLOR = neo_pixels.combine_color_and_brightness(neo_pixels.KEYCOLORBASE, 0.1)

class Note(Input):
    name = "note"
    label = "NOTE"
    def __init__(self, module_helper, slot_id):
        self.layout = NOTE_LAYOUT_KEY
        super().__init__(module_helper, slot_id, include_musical_parms=True)

    def create_main_parms(self):
        parms = super().create_main_parms()
        parms.append(Parms.Parm(name="layout", label="LAY", parm_type=Parms.EnumParmType, default=self.layout,
                                options=NOTE_LAYOUT_OPTIONS, edit_callback_function=self.set_layout))
        return parms

    def set_layout(self, value):
        self.layout = value
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()
        return value

    def color_pixels(self, color_overrides=None):
        if self.layout == NOTE_LAYOUT_KEY:
            super().color_pixels(color_overrides)
            return

        color_array = [OFF_SCALE_COLOR] * 12
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)
        scale_steps = set(scale)

        for pad in range(12):
            semitone = pad + self.state.key_offset
            degree = semitone % 12
            color = OFF_SCALE_COLOR
            if degree == 0:
                color = neo_pixels.KEYCOLORROOT
            elif degree in scale_steps:
                color = neo_pixels.KEYCOLORBASE
            color_array[PADMAP.index(pad)] = color

        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

    def convert_chromatic_note(self, pad_note):
        pad = PADMAP.index(pad_note)
        return self.state.key + pad + self.state.key_offset + (self.state.octave + 2) * 12

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            if self.layout == NOTE_LAYOUT_CHROMATIC:
                note = self.convert_chromatic_note(pad_note)
            else:
                note = self.convert_note(pad_note, scale, scale_notes)
            self.note_ons_out.append_value(note, velocity = self.velocity)

        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            if self.layout == NOTE_LAYOUT_CHROMATIC:
                note = self.convert_chromatic_note(pad_note)
            else:
                note = self.convert_note(pad_note, scale, scale_notes)
            self.note_offs_out.append_value(note)

        return self.note_ons_out, self.note_offs_out
        
