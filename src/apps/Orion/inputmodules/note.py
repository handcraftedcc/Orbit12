from ..core._modules._input import Input

from ..core import music as Music



class Note(Input):
    name = "note"
    label = "NOTE"
    help_text = "IN KEY NOTE LYT"
    def __init__(self, module_helper, slot_id):
        super().__init__(module_helper, slot_id, include_musical_parms=True)

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            self.note_ons_out.append_value(self.convert_note(pad_note, scale, scale_notes), velocity = self.velocity)

        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            self.note_offs_out.append_value(self.convert_note(pad_note, scale, scale_notes))

        return self.note_ons_out, self.note_offs_out
        
