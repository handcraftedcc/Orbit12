from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.timing_clock import TimingClock
from adafruit_midi.control_change import ControlChange
from .constants import POLYPHONY
from .note_array import NoteArray, NoteOnArray, NoteOffArray

class OutputManager:
    def __init__(self, macropad, state):
        self.macropad = macropad
        self.state = state

        self.note_ons_out = NoteOnArray(length = POLYPHONY*2, channels = True)
        self.note_offs_out = NoteOffArray(length = POLYPHONY*2, channels = True)
        self.held_notes = NoteArray(length = POLYPHONY*2, channels = True)
        self.midi_start = False
        self.midi_stop = False
        self.pending_midi_clock_ticks = 0

    def schedule_midi_notes(self, note_ons, note_offs, channel = 0):

        for i in range(note_ons.length):
            note = note_ons.notes[i]

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            if channel is None:
                channel = 0

            if not self.held_notes.contains(note, channel=channel):
                self.held_notes.append_value(note, velocity=velocity, channel=channel)

            if not self.note_ons_out.contains(note, channel=channel):
                self.note_ons_out.append_value(note, velocity=velocity, channel=channel)

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if channel is None: channel = 0
            if self.held_notes.contains(note, channel=channel):
                self.held_notes.remove_value_first(note, channel=channel)

            if not self.note_offs_out.contains(note, channel=channel) and not self.held_notes.contains(note,channel=channel):
                if self.note_offs_out.length < self.note_offs_out.max_length:
                    self.note_offs_out.append_value(note, channel = channel)
                else: # Fallback
                    self.macropad.midi.send(self.macropad.NoteOff(note, 0), channel=channel)


                    #print("held notes: ", self.held_notes)

    def schedule_midi_clock(self):
        self.pending_midi_clock_ticks += 1

    def process_midi_clock_out(self):
        while self.pending_midi_clock_ticks > 0:
            self.macropad.midi.send(TimingClock())
            self.pending_midi_clock_ticks -= 1

    def schedule_midi_start(self): #send immediately
        self.macropad.midi.send([Start(),TimingClock()])

    def schedule_midi_stop(self):
        self.macropad.midi.send(Stop())

    def all_notes_off(self, send_cc=True):
        self.note_ons_out.clear()

        for i in range(self.held_notes.length):
            held_note = self.held_notes.notes[i]
            held_channel = self.held_notes.channels[i]
            if not self.note_offs_out.append_value(held_note, channel=held_channel):
                self.macropad.midi.send(self.macropad.NoteOff(held_note, 0), channel=held_channel)

        self.held_notes.clear()

        #if send_cc:
        #    for channel in range(16):
        #        self.macropad.midi.send(ControlChange(123, 0), channel=channel)

    def panic(self):
        for channel in range(16):
            for note in range(127):
                self.macropad.midi.send(self.macropad.NoteOff(note, 0), channel=channel)
                self.process_midi_out()

    def process_midi_out(self):
        # Send Midi Notes
        for i in range(self.note_ons_out.length):
            note = self.note_ons_out.notes[i]
            if self.note_ons_out.velocities is not None:
                velocity = self.note_ons_out.velocities[i]
            else:
                velocity = 127
            channel = self.note_ons_out.channels[i]
            self.macropad.midi.send(self.macropad.NoteOn(note, velocity), channel = channel)  # send midi note_on
        for i in range(self.note_offs_out.length):
            note = self.note_offs_out.notes[i]
            channel = self.note_offs_out.channels[i]
            self.macropad.midi.send(self.macropad.NoteOff(note, 0), channel = channel)  # send midi note_off

        self.note_ons_out.clear()
        self.note_offs_out.clear()
