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
        self.offsets = [0,2,4,0,0]
        self.note_relationship = NoteRelationshipArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
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

        scale_aware_parm = Parms.Parm(
            name="scale_aware",
            label="Scale Aware",
            default=True,
            parm_type=Parms.BooleanParmType,
            edit_callback_function=self.set_scale_aware,
        )

        parms.append(scale_aware_parm)

        return parms

    def set_offset(self, index, value):
        self.offsets[index] = value

    def set_scale_aware(self, value):
        self.scale_aware = value

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        if self.scale_aware:
            scale = music.SCALES[self.module_helper.state.scale]
        else:
            scale = None

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            off_notes = self.note_relationship.remove_note_all(note)
            self.note_offs_out.append_values(off_notes)

        for i in range(note_ons.length):
            note = note_ons.notes[i]

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            for idx,offset in enumerate(self.offsets):
                if offset in self.offsets[:idx]:
                        continue #skip duplicate notes
                new_note = music.transpose(note, offset, 0, self.scale_aware, self.state.key, scale)
                append = False
                if self.note_relationship.in_array.length < self.note_relationship.in_array.max_length:
                    append = True
                elif (self.note_relationship.in_array.length == self.note_relationship.in_array.max_length and
                    self.note_relationship.out_array.length < self.note_relationship.out_array.max_length):
                    off_note = self.note_relationship.remove_note_single(self.note_relationship.in_array.notes[0])
                    self.note_offs_out.append_value(off_note)
                    append = True

                if append:
                        has_out_note,out_note_id = self.note_relationship.has_out_note(new_note)
                        if has_out_note:
                            self.note_relationship.replace_note_in_index(note, out_note_id)
                        else:
                            self.note_ons_out.append_value(new_note, velocity=velocity)
                            self.note_relationship.add_note(note, new_note)


        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_relationship.clear()