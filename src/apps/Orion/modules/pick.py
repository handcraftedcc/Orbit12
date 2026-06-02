from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteArray

PICK_MODE_OPTIONS = (
    "1ST",
    "2ND",
    "3RD",
    "LAST",
    "LOW",
    "HIGH",
)

class Pick(Module):
    name = "pick"
    label = "PICK"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.pick_mode = 0
        self.held_note = None
        self.held_notes_in = NoteArray()
        self.note_ons_out = NoteArray()
        self.note_offs_out = NoteArray()
        self.octaves = 0
        self.held_note_octave = 0

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        # Pick Mode
        mode_parm = Parms.Parm(name="pick_mode", label="MDE", default=self.pick_mode, parm_type=Parms.EnumParmType,
                               options=PICK_MODE_OPTIONS, bind_object=self, bind_attribute="pick_mode")
        parms.append(mode_parm)

        octaves_parm = Parms.Parm(name="octaves", label="OCT", default=self.octaves, parm_type=Parms.IntParmType,
                                  increment=1, bind_object=self, bind_attribute="octaves")
        parms.append(octaves_parm)

        return parms

    def process(self, note_ons, note_offs):
        self.held_notes_in.append_values(note_ons)
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.held_notes_in.remove_value_first(note)

        old_note = None
        if self.held_notes_in.length > 0:
            index = 0
            if self.pick_mode == 0:
                index = 0
            if self.pick_mode == 1:
                index = min(1,self.held_notes_in.length-1)
            if self.pick_mode == 2:
                index = min(2,self.held_notes_in.length-1)
            if self.pick_mode == 3:
                index = self.held_notes_in.length - 1
            if self.pick_mode == 4:
                return_note, index = self.held_notes_in.get_min_note()
            if self.pick_mode == 5:
                return_note, index = self.held_notes_in.get_max_note()

            return_note = self.held_notes_in.notes[index] + self.octaves*12
            if return_note != self.held_note: #New note detected
                velocity = 127
                if self.held_notes_in.velocities is not None:
                    velocity = self.held_notes_in.velocities[index]
                pass
                self.note_ons_out.append_value(return_note, velocity=velocity)
                if self.held_note: old_note = self.held_note
                self.held_note = return_note
                self.held_note_octave = self.octaves
            else:
                pass
        else:
            pass

        length = note_offs.length
        for i in range(length):
            note = note_offs.notes[i]

            if self.held_note is None:
                break

            if self.held_note - self.held_note_octave*12 == note:
                self.note_offs_out.clear()
                self.note_offs_out.append_value(note + self.held_note_octave*12)
                self.held_note = None
                self.held_note_octave = None
                break

        if old_note: self.note_offs_out.append_value(old_note)

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes_in.clear()
        self.held_note = None
        self.held_note_octave = 0

    def remove(self):
        super().remove()
        self.held_notes_in = None
