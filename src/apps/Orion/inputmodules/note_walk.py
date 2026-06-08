from ..core import music as Music
from ..core._modules._input import Input, PADMAP
from ..core.note_array import NoteArray


### NOTE WALK INPUT ###

class NoteWalk(Input):
    name = "note_walk"
    label = "NOTEWLK"

    def __init__(self, module_helper, slot_id):
        self.held_pads = NoteArray(length=12, velocities=True)
        self.note_root = None
        self.offset_root = None
        self.current_steps = 0
        self.active_note = None
        super().__init__(module_helper, slot_id, include_musical_parms=True)

    ### WALK STATE ###

    def pad_index(self, pad_note):
        return PADMAP.index(pad_note)

    def add_held_pad(self, pad, velocity):
        if self.held_pads.contains(pad):
            self.held_pads.remove_value_first(pad)
        self.held_pads.append_value(pad, velocity=velocity)

    def remove_held_pad(self, pad):
        self.held_pads.remove_value_first(pad)
        if self.offset_root == pad and self.held_pads.length > 0:
            self.offset_root = self.held_pads.notes[self.held_pads.length - 1]

    def reset_walk(self):
        self.held_pads.clear()
        self.note_root = None
        self.offset_root = None
        self.current_steps = 0
        self.active_note = None

    def note_for_steps(self):
        scale = Music.SCALES[self.state.scale]
        while True:
            note = Music.transpose(self.note_root, self.current_steps, 0, True, self.state.key, scale)
            if note > 127:
                self.current_steps -= 1
            elif note < 0:
                self.current_steps += 1
            else:
                return note

    def set_active_note(self, note, velocity):
        if self.active_note is not None and self.active_note != note:
            self.note_offs_out.append_value(self.active_note)
        if self.active_note != note:
            self.note_ons_out.append_value(note, velocity=velocity)
        self.active_note = note

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_offs.length):
            self.remove_held_pad(self.pad_index(note_offs.notes[i]))

        if self.held_pads.length == 0 and self.active_note is not None:
            self.note_offs_out.append_value(self.active_note)
            self.reset_walk()

        for i in range(note_ons.length):
            pad = self.pad_index(note_ons.notes[i])
            velocity = note_ons.velocity_at(i, self.velocity)
            if self.held_pads.length == 0:
                scale = Music.SCALES[self.state.scale]
                self.note_root = self.convert_note(note_ons.notes[i], scale, len(scale))
                self.offset_root = pad
                self.current_steps = 0
            else:
                self.current_steps += pad - self.offset_root
            self.add_held_pad(pad, velocity)
            self.set_active_note(self.note_for_steps(), velocity)

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        super().stop()
        self.reset_walk()

    def remove(self):
        super().remove()
        self.held_pads = None
