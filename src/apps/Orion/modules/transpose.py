from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core.note_array import NoteRelationshipArray,NoteOnArray,NoteOffArray

def find_closest(values, target):
    def distance_from_target(index):
        return abs(values[index] - target)

    index = min(range(len(values)), key=distance_from_target)
    value = values[index]

    return value, index

class Transpose(Module):
    name = "transpose"
    label = "Transpose"
    version = 1
    def __init__(self, module_helper, slot_id):
        parms = []
        super().__init__(module_helper, slot_id, parms)

        # Amount
        self.amount_parm = Parms.Parm(name="amount", label="Amount", default=0, parm_type=Parms.IntParmType,
                                    increment=1, edit_callback_function=self.set_amount)
        self.parms.append(self.amount_parm)

        # Scale Aware
        self.scale_aware_parm = Parms.Parm(name="scale_aware", label="Scale Aware", default=True, parm_type=Parms.BooleanParmType,
                                      edit_callback_function=self.set_scale_aware)
        self.parms.append(self.scale_aware_parm)

        self.note_relationship = NoteRelationshipArray()

        self.transpose_amount = 0
        self.scale_aware = True

    def set_amount(self, value):
        self.transpose_amount = value

    def set_scale_aware(self, value):
        self.scale_aware = value

    def transpose(self, note, scale):
        if self.scale_aware:
            normalized_note = note % 12
            octave = note // 12
            closest_note,closest_index = find_closest(scale,normalized_note)
            new_index = closest_index+self.transpose_amount
            scale_notes = len(scale)
            octave_shift = new_index // scale_notes
            new_index = new_index - octave_shift * scale_notes
            note = scale[new_index]
            note = note + (octave+octave_shift)*12
        else:
            note = note + self.transpose_amount
        return note

    def process(self, note_ons:NoteOnArray, note_offs:NoteOffArray):
        if self.scale_aware:
            scale = music.SCALES[self.module_helper.state.scale]
        else:
            scale = None
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            new_note = self.transpose(note, scale)
            note_ons.notes[i] = new_note
            self.note_relationship.add_note(note, new_note)
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            off_note = self.note_relationship.remove_note_single(note)
            if off_note is not None: note_offs.notes[i] = off_note

        return  note_ons, note_offs
