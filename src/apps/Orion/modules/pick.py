from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteOnArray, NoteOffArray

PICK_MODE_OPTIONS = (
    "STRT",
    "END",
    "LOW",
    "HIGH",
)

PICK_MODE_START, PICK_MODE_END, PICK_MODE_LOW, PICK_MODE_HIGH = 0, 1, 2, 3

### PICK MODULE ###

class Pick(Module):
    name = "pick"
    label = "PICK"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.pick_mode = PICK_MODE_START
        self.count = 1
        self.invert = 0
        self.offset = 0
        self.held_notes_in = NoteOnArray()
        self.active_notes = NoteOnArray()
        self.next_notes = NoteOnArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.octaves = 0

        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        parms = []
        parms.append(Parms.Parm(name="pick_mode", label="MDE", default=self.pick_mode,
                                parm_type=Parms.EnumParmType, options=PICK_MODE_OPTIONS,
                                bind_object=self, bind_attribute="pick_mode"))
        parms.append(Parms.Parm(name="count", label="COUNT", default=self.count,
                                parm_type=Parms.IntParmType, minmax=(1, 12),
                                bind_object=self, bind_attribute="count"))
        parms.append(Parms.Parm(name="invert", label="INV", default=self.invert,
                                parm_type=Parms.BooleanParmType, bind_object=self, bind_attribute="invert"))
        parms.append(Parms.Parm(name="offset", label="OFST", default=self.offset,
                                parm_type=Parms.IntParmType, minmax=(0, 12),
                                bind_object=self, bind_attribute="offset"))
        parms.append(Parms.Parm(name="octaves", label="OCT", default=self.octaves,
                                parm_type=Parms.IntParmType, increment=1,
                                bind_object=self, bind_attribute="octaves"))
        return parms

    ### SELECTION ###

    def ordered_indexes(self):
        indexes = list(range(self.held_notes_in.length))
        if self.pick_mode == PICK_MODE_END:
            indexes.reverse()
        elif self.pick_mode in (PICK_MODE_LOW, PICK_MODE_HIGH):
            indexes.sort(key=lambda i: self.held_notes_in.notes[i])
            if self.pick_mode == PICK_MODE_HIGH:
                indexes.reverse()
        return indexes

    def append_next_note(self, index):
        note = self.held_notes_in.notes[index] + self.octaves * 12
        self.next_notes.append_value(note, velocity=self.held_notes_in.velocity_at(index))

    def append_selected_notes(self, selected_indexes):
        if not self.invert:
            for index in selected_indexes:
                self.append_next_note(index)
            return

        selected_lookup = {}
        for index in selected_indexes:
            selected_lookup[index] = True
        for index in self.ordered_indexes():
            if index in selected_lookup:
                continue
            self.append_next_note(index)

    def build_next_notes(self):
        length = self.held_notes_in.length
        self.next_notes.clear()
        if length == 0:
            return

        order = self.ordered_indexes()
        count = min(max(0, self.count), length)
        offset = max(0, self.offset)
        selected = order[offset:offset + count]
        self.append_selected_notes(selected)

    def selection_changed(self):
        if self.active_notes.length != self.next_notes.length:
            return True
        for i in range(self.active_notes.length):
            if (self.active_notes.notes[i] != self.next_notes.notes[i] or
                    self.active_notes.velocity_at(i) != self.next_notes.velocity_at(i)):
                return True
        return False

    def emit_selection_change(self):
        for i in range(self.active_notes.length):
            self.note_offs_out.append_value(self.active_notes.notes[i])
        self.note_ons_out.append_values(self.next_notes)
        self.active_notes.clear()
        self.active_notes.append_values(self.next_notes)

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        # Maintain the full held input stack, then emit the selected output set.
        self.held_notes_in.append_values(note_ons)
        for i in range(note_offs.length):
            self.held_notes_in.remove_value_first(note_offs.notes[i])

        self.build_next_notes()
        if self.selection_changed():
            self.emit_selection_change()

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes_in.clear()
        self.active_notes.clear()

    def remove(self):
        super().remove()
        self.held_notes_in = None
        self.active_notes = None
        self.next_notes = None
