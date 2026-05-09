from ..core.module import Module
from ..core import parms as Parms
from ..core._modules._input import Input

from ..core import music as Music



class Note(Input):
    name = "note"
    label = "Note"
    def __init__(self, state, slot_id):
        parms = []
        super().__init__(state, slot_id, include_musical_parms=True)

    def process(self, note_ons, note_offs, velocities):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.velocities_out.clear()

        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        for pad_note in note_ons:
            self.note_ons_out.append(self.convert_note(pad_note, scale, scale_notes))
            self.velocities_out.append(self.velocity)

        for pad_note in note_offs:
            self.note_offs_out.append(self.convert_note(pad_note, scale, scale_notes))

        return self.note_ons_out, self.note_offs_out, self.velocities_out
        
