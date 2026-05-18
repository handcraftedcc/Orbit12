from ..core.constants import POLYPHONY
from ..core.module import Module
from ..core import parms as Parms
import adafruit_ticks as ticks
import random
from ..core.note_array import NoteArray,NoteOnArray,NoteOffArray

def shuffle_in_place(values):
    for i in range(len(values) - 1, 0, -1):
        j = random.randint(0, i)
        values[i], values[j] = values[j], values[i]

class Arp(Module):
    name = "arp"
    label = "Arp"
    version = 1
    def __init__(self, module_helper, slot_id):
        #TODO: Rewrite to new Note Array Format
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        # Setup Attribs
        self.rate = 1
        self.note_register = NoteOnArray()
        self.note_register_position = 0
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_offs = NoteArray(times = True)
        self.popped_ids = NoteArray()
        self.mode = None
        self.mode_list = [
            "up",
            "down",
            "random",
            "inOrder",
            "repeat",
        ]
        self.retrigger_mode = 0
        self.retrigger_mode_list = [
            "Retrigger",
            "Continuous",
            "Stable",
        ]
        self.random_seed = 0
        self.gate = 0
        self.arp_state = 0 #0 = Not active, 1 = Active -> When all notes are released we go empty
        self.held_notes = NoteArray()

        self.last_midi_tick = self.transport.midi_tick
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

        # Init values
        self.rate = self.rate_parm.get_actual_value()
        self.mode = self.mode_parm.get_actual_value()
        self.gate = self.gate_parm.get_actual_value()

    def create_main_parms(self):
        parms = []
        # Rate
        self.rate_parm = Parms.Parm(name="rate", label="Rate", default=8, parm_type=Parms.RateParmType,
                                    edit_callback_function=self.set_rate)
        parms.append(self.rate_parm)

        # Mode
        self.mode_parm = Parms.Parm(name="mode", label="Mode", default=0, parm_type=Parms.EnumParmType,
                                    options=self.mode_list,edit_callback_function=self.set_mode)
        parms.append(self.mode_parm)

        # Gate
        self.gate_parm = Parms.Parm(name="gate", label="Gate", default=100, parm_type=Parms.FloatParmType,
                                    increment=5, edit_callback_function=self.set_gate)
        parms.append(self.gate_parm)

        # Retrigger Mode
        self.retrigger_parm = Parms.Parm(name="retrigger_mode", label="Retrigger", default=0, parm_type=Parms.EnumParmType,
                                    options = self.retrigger_mode_list, edit_callback_function=self.set_retrigger_mode)
        parms.append(self.retrigger_parm)
        return parms

    def set_rate(self, value):
        self.rate = self.rate_parm.get_actual_value()
        return self.rate

    def set_mode(self, value):
        self.mode = self.mode_parm.get_actual_value()
        return self.mode

    def set_gate(self, value):
        self.gate = self.gate_parm.get_actual_value()
        return self.gate_parm.get_actual_value()

    def set_retrigger_mode(self, value):
        self.retrigger_mode = value
        return self.retrigger_mode

    def update_random_seed(self):
        self.random_seed += 1
        return self.random_seed

    def rate_to_midi_ticks(self):
        # RATE_VALUES are in 1/16-note units; 1/16 = 6 MIDI clock ticks
        return max(1, int(round(self.rate * 6)))

    def update_note_register(self, note_ons, note_offs):
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            self.held_notes.append_value(note)
            if not self.note_register.contains(note):
                self.note_register.append_value(note)
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            if self.note_register.contains(note) and not self.held_notes.contains(note):
                self.note_register.remove_value_first(note)
        if self.mode == self.mode_list.index("up"):
            self.note_register.sort_notes()
        elif self.mode == self.mode_list.index("down"):
            self.note_register.sort_notes()
            self.note_register.reverse_notes()
        elif self.mode == self.mode_list.index("random"):
            self.note_register.randomize_notes(self.random_seed)

    def generate_notes(self):
        #Calculate timing
        current = self.transport.now
        scheduled = ticks.ticks_add(current, self.gate)

        if self.mode == self.mode_list.index("repeat"):
            for i in range(self.note_register.length):
                note = self.note_register.notes[i]
                if self.note_register.velocities:
                    velocity = self.note_register.velocities[i]
                else:
                    velocity = 127
                self.note_ons_out.append_value(note,velocity=velocity)
                self.scheduled_offs.append_value(note, time=scheduled)
        else:
            register_note_count = self.note_register.length
            if self.retrigger_mode == 2:
                midi_tick = self.transport.midi_tick
                self.note_register_position = int(midi_tick % (self.rate_to_midi_ticks()*register_note_count)/register_note_count)
            index = self.note_register_position % register_note_count
            note = self.note_register.notes[index]
            if self.note_register.velocities:
                velocity = self.note_register.velocities[index]
            else:
                velocity = 127
            self.note_ons_out.append_value(note, velocity=velocity)
            self.scheduled_offs.append_value(note, time=scheduled)
            self.note_register_position = (self.note_register_position+1) % register_note_count

        #print("Note Register", self.note_register.notes)
        #print("Note Register Position", self.note_register_position)

    def process_note_offs(self, force_all = False):
        #TODO: Maybe build a time removal thing into the note_array directly
        current = self.transport.now
        self.popped_ids.clear()
        if not force_all:
            for i in range(self.scheduled_offs.length):
                note_off = self.scheduled_offs.notes[i]
                off_time = self.scheduled_offs.times[i]
                if ticks.ticks_less(off_time, current):
                    self.note_offs_out.append_value(note_off)
                    self.popped_ids.append_value(i)
        else: #Send note offs for all
            for i in range(self.scheduled_offs.length):
                note_off = self.scheduled_offs.notes[i]
                self.note_offs_out.append_value(note_off)
                self.popped_ids.append_value(i)

        for i in range(self.popped_ids.length-1, -1, -1):
            this_id = self.popped_ids.notes[i]
            self.scheduled_offs.remove_index(this_id)

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        running = self.transport.running
        if note_ons.length>0 or note_offs.length>0:
            self.update_note_register(note_ons, note_offs)

        if self.arp_state == 1 and self.note_register.length<1: #All keys released
            self.arp_state = 0
            #print("All Keys Released")
            if self.retrigger_mode == 0:
                self.note_register_position = 0
            self.update_random_seed()

        if self.arp_state == 0 and self.note_register.length>0: #From no keys pressed -> keys pressed
            self.arp_state = 1
            self.last_midi_tick = self.transport.midi_tick

        if self.arp_state == 1 and self.transport.running:
            interval = self.rate_to_midi_ticks()  # e.g. 6 for 1/16
            last_tick = self.last_midi_tick
            now_tick = self.transport.midi_tick
            if now_tick < last_tick: #On transport reset
                self.last_midi_tick = now_tick
                last_tick = now_tick

            # detect transport start edge
            if running and not self.was_transport_running:
                # re-anchor arp timing to new transport tick origin
                self.last_midi_tick = self.transport.midi_tick

                # restart pattern position on transport start
                if self.retrigger_mode == 0:
                    self.note_register_position = 0

                # emit immediately if notes are held
                if self.note_register.length>0:
                    self.generate_notes()

            if now_tick > last_tick:
                prev_bin = last_tick // interval
                curr_bin = now_tick // interval
                triggers = curr_bin - prev_bin  # how many grid steps crossed

                for _ in range(triggers):
                    self.generate_notes()

                self.last_midi_tick = now_tick
        self.process_note_offs()

        self.was_transport_running = running

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.process_note_offs(force_all = True)
        self.held_notes.clear()






