from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.timing_clock import TimingClock


class OutputManager:
    def __init__(self, macropad, state):
        self.macropad = macropad
        self.state = state

        self.note_ons_out = []
        self.note_offs_out = []
        self.velocities_out = []
        self.held_notes = []
        self.pending_midi_clock_ticks = 0
        self.midi_start = False
        self.midi_stop = False

    def schedule_midi_notes(self, note_ons, note_offs, velocities):
        self.held_notes.extend(note_ons)
        for idx,note in enumerate(note_ons):
            if note not in self.note_ons_out:
                self.note_ons_out.append(note)
                self.velocities_out.append(velocities[idx])

        for note in note_offs:
            if note in self.held_notes:
                self.held_notes.remove(note)
            if note not in self.note_offs_out and note not in self.held_notes:
                self.note_offs_out.append(note)

        #print("held notes: ", self.held_notes)

    def schedule_midi_clock(self): #send immediately
        self.pending_midi_clock_ticks += 1
        self.macropad.midi.send(TimingClock())

    def schedule_midi_start(self): #send immediately
        self.macropad.midi.send([Start(),TimingClock()])


    def schedule_midi_stop(self):
        self.macropad.midi.send(Stop())


    def all_notes_off(self):
        for held_note in self.held_notes:
            self.note_offs_out.append(held_note)
        self.held_notes.clear()

    def process_midi_out(self):
        # Send Midi Notes
        for idx, note in enumerate(self.note_ons_out):
            if self.velocities_out[idx]:
                velocity = self.velocities_out[idx]
            else:
                velocity = 127
            self.macropad.midi.send(self.macropad.NoteOn(note, velocity))  # send midi note_on
        for note in self.note_offs_out:
            self.macropad.midi.send(self.macropad.NoteOff(note, 0))  # send midi note_off

        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.velocities_out.clear()