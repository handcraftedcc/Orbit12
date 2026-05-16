from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteArray,NoteOnArray,NoteOffArray

class Pick(Module):
    name = "pick"
    label = "Pick"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.pick_mode = 0
        self.held_note = None
        self.held_notes_in = NoteArray()

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        # Pick Mode
        pick_mode_options = [
            "1st",
            "2nd",
            "3rd",
            "Last",
            "Lowest",
            "Highest"
        ]
        self.mode_parm = Parms.Parm(name="pick_mode", label="Mode", default=0, parm_type=Parms.EnumParmType,
                                      options=pick_mode_options, edit_callback_function=self.set_pick_mode)
        parms.append(self.mode_parm)
        return parms

    def set_pick_mode(self, value):
        self.pick_mode = value

    def process(self, note_ons:NoteOnArray, note_offs:NoteOffArray):
        self.held_notes_in.append_values(note_ons)
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

            return_note = self.held_notes_in.notes[index]
            if return_note != self.held_note: #New note detected
                velocity = 127
                if self.held_notes_in.velocities is not None:
                    velocity = self.held_notes_in.velocities[index]
                note_ons.clear()
                note_ons.append_value(return_note, velocity=velocity)
                if self.held_note: old_note = self.held_note
                self.held_note = return_note
            else:
                note_ons.clear()
        else:
            note_ons.clear()

        length = note_offs.length
        for i in range(length):
            note = note_offs.notes[i]
            if self.held_note == note:
                note_offs.clear()
                note_offs.append_value(note)
                self.held_note = None
                break

        if old_note: note_offs.append_value(old_note)

        return note_ons, note_offs



