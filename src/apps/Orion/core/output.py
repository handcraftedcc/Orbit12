class OutputManager:
    def __init__(self, macropad, state):
        self.macropad = macropad
        self.state = state

        self.note_ons_out = []
        self.note_offs_out = []
        self.velocities_out = []
        self.held_notes = []

    def schedule_midi(self, note_ons, note_offs, velocities):
        self.held_notes.append(note_ons)
        for note in note_ons:
            if note not in self.note_ons_out:
                self.note_ons_out.append(note)
                self.velocities_out.append(velocities)

        for note in note_ons:
            if note in self.held_notes:
                self.held_notes.remove(note)
            if note not in self.note_offs_out and note not in self.held_notes:
                self.note_offs_out.append(note_offs)

    def process_midi_out(self):
        for idx, note in enumerate(self.note_ons_out):
            if self.velocities_out[idx]:
                velocity = self.velocities_out[idx]
            else:
                velocity = 127
            self.macropad.midi.send(self.macropad.NoteOn(note, velocity))  # send midi note_on
        for note in note_offs:
            self.macropad.midi.send(self.macropad.NoteOff(note, 0))  # send midi note_off

        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.velocities_out.clear()