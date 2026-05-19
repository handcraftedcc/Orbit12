from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core.note_array import NoteRelationshipArray,NoteOnArray,NoteOffArray

class Transpose(Module):
    name = "transpose"
    label = "Transpose"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.note_relationship = NoteRelationshipArray()
        self.semitones = 0
        self.octaves = 0
        self.scale_aware = True
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        # Amount
        self.semitones_parm = Parms.Parm(name="semitones", label="Semitones", default=0, parm_type=Parms.IntParmType,
                                         increment=1, edit_callback_function=self.set_semitones)
        parms.append(self.semitones_parm)

        # Octaves
        self.octaves_parm = Parms.Parm(name="octaves", label="Octaves", default=0, parm_type=Parms.IntParmType,
                                      increment=1, edit_callback_function=self.set_octaves)
        parms.append(self.octaves_parm)

        # Scale Aware
        self.scale_aware_parm = Parms.Parm(name="scale_aware", label="Scale Aware", default=True, parm_type=Parms.BooleanParmType,
                                      edit_callback_function=self.set_scale_aware)
        parms.append(self.scale_aware_parm)
        return parms

    def set_semitones(self, value):
        self.semitones = value

    def set_octaves(self, value):
        self.octaves = value

    def set_scale_aware(self, value):
        self.scale_aware = value

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        if self.scale_aware:
            scale = music.SCALES[self.module_helper.state.scale]
        else:
            scale = None

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            new_note = music.transpose(note, self.semitones, self.octaves, self.scale_aware, self.state.key, scale)

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            self.note_ons_out.append_value(new_note, velocity=velocity)
            self.note_relationship.add_note(note, new_note)

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            off_note = self.note_relationship.remove_note_single(note)
            if off_note is not None:
                self.note_offs_out.append_value(off_note)

        return self.note_ons_out, self.note_offs_out
