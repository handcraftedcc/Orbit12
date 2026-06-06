from ..core.module import Module
from ..core import parms as Parms
from ..core.constants import POLYPHONY
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray


class Latch(Module):
    name = "latch"
    label = "LATCH"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.polyphony = POLYPHONY
        self.held_notes = NoteArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        return [Parms.Parm(name="polyphony", label="POLY", default=self.polyphony,
                           parm_type=Parms.IntParmType, minmax=(1, POLYPHONY),
                           bind_object=self, bind_attribute="polyphony")]

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        polyphony = min(self.polyphony, self.held_notes.max_length)

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
                self.note_offs_out.append_value(note)
                continue

            if self.held_notes.length >= polyphony:
                self.note_offs_out.append_value(self.held_notes.notes[0])
                self.held_notes.remove_index(0)

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]
            self.held_notes.append_value(note)
            self.note_ons_out.append_value(note, velocity=velocity)

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.held_notes.clear()
        self.note_ons_out.clear()
        self.note_offs_out.clear()

    def remove(self):
        super().remove()
        self.held_notes = None
