from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core.note_array import NoteRelationshipArray, NoteOnArray, NoteOffArray

def find_closest(values, target):
    def distance_from_target(index):
        return abs(values[index] - target)

    index = min(range(len(values)), key=distance_from_target)
    value = values[index]
    return value, index

class Chord(Module):
    name = "chord"
    label = "Chord"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.offsets = [0, 2, 4, 0, 0]  # or whatever defaults you want
        self.note_relationship = NoteRelationshipArray(length=15)
        self.note_ons_out = NoteOnArray(length=15)
        self.note_offs_out = NoteOffArray(length=15)
        self.scale_aware = True

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        self.offset_parms = []

        for i in range(5):
            offset_parm = Parms.Parm(
                name="offset" + str(i+1),
                label="Offset " + str(i+1),
                default=self.offsets[i],
                parm_type=Parms.IntParmType,
                increment=1,
                edit_callback_function=lambda value, index=i: self.set_offset(index, value),
            )
            self.offset_parms.append(offset_parm)

        parms.extend(self.offset_parms)
        return parms

    def set_offset(self, index, value):
        self.offsets[index] = value

    def set_scale_aware(self, value):
        self.scale_aware = value

    def transpose(self, semitones, note, scale):
        if self.scale_aware:
            normalized_note = note % 12
            octave = note // 12
            closest_note,closest_index = find_closest(scale,normalized_note)
            new_index = closest_index+semitones
            scale_notes = len(scale)
            octave_shift = new_index // scale_notes
            new_index = new_index - octave_shift * scale_notes
            note = scale[new_index]
            note = note + (octave+octave_shift)*12
        else:
            note = note + semitones
        return note

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        if self.scale_aware:
            scale = music.SCALES[self.module_helper.state.scale]
        else:
            scale = None

        for i in range(note_ons.length):
            note = note_ons.notes[i]

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            voice_count = len(self.offsets)

            if self.has_output_capacity(note_on_count=voice_count, relationship_count=voice_count):
                for offset in self.offsets:
                    new_note = self.transpose(offset, note, scale)
                    self.note_ons_out.append_value(new_note, velocity=velocity)
                    self.note_relationship.add_note(note, new_note)

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            off_notes = self.note_relationship.remove_note_all(note)
            self.note_offs_out.append_values(off_notes)

        return self.note_ons_out, self.note_offs_out