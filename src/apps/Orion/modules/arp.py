from ..core.module import Module
from ..core import parms as Parms
import adafruit_ticks as ticks
import random

def shuffle_in_place(values):
    for i in range(len(values) - 1, 0, -1):
        j = random.randint(0, i)
        values[i], values[j] = values[j], values[i]

class Arp(Module):
    name = "arp"
    label = "Arp"
    version = 1
    def __init__(self, module_helper, slot_id):
        parms = []
        super().__init__(module_helper, slot_id, parms)

        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        # Setup Attribs
        self.rate = 1
        self.note_register = []
        self.note_register_velocities = []
        self.note_register_position = 0
        self.note_ons_out = []
        self.note_offs_out = []
        self.velocities_out = []
        self.scheduled_offs = []
        self.scheduled_offs_time = []
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
        self.held_notes = []

        self.last_midi_tick = self.transport.midi_tick
        self.was_transport_running = self.transport.running


        # Setup Parms

        # Rate
        self.rate_parm = Parms.Parm(name="rate", label="Rate", default=8, parm_type=Parms.RateParmType,
                                    edit_callback_function=self.set_rate)
        self.parms.append(self.rate_parm)

        # Mode
        self.mode_parm = Parms.Parm(name="mode", label="Mode", default=0, parm_type=Parms.EnumParmType,
                                    options=self.mode_list,edit_callback_function=self.set_mode)
        self.parms.append(self.mode_parm)

        # Gate
        self.gate_parm = Parms.Parm(name="gate", label="Gate", default=100, parm_type=Parms.FloatParmType,
                                    increment=5, edit_callback_function=self.set_gate)
        self.parms.append(self.gate_parm)

        # Retrigger Mode
        self.retrigger_parm = Parms.Parm(name="retrigger_mode", label="Retrigger", default=0, parm_type=Parms.EnumParmType,
                                    options = self.retrigger_mode_list, edit_callback_function=self.set_retrigger_mode)
        self.parms.append(self.retrigger_parm)

        # Init values
        self.rate = self.rate_parm.get_actual_value()
        self.mode = self.mode_parm.get_actual_value()
        self.gate = self.gate_parm.get_actual_value()

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

    def update_note_register(self, note_ons, note_offs, velocities):
        for note in note_ons:
            self.held_notes.append(note)
            if note not in self.note_register:
                self.note_register.append(note)
        for note in note_offs:
            if note in self.held_notes:
                self.held_notes.remove(note)
            if note in self.note_register and note not in self.held_notes:
                self.note_register.remove(note)
        if self.mode == self.mode_list.index("up"):
            self.note_register.sort()
        elif self.mode == self.mode_list.index("down"):
            self.note_register.sort()
            self.note_register.reverse()
        elif self.mode == self.mode_list.index("random"):
            random.seed(self.random_seed)
            shuffle_in_place(self.note_register)

    def generate_notes(self):
        #Calculate timing
        current = ticks.ticks_ms()
        scheduled = ticks.ticks_add(self.gate, current)

        if self.mode == self.mode_list.index("repeat"):
            for note in self.note_register:
                self.note_ons_out.append(note)
                self.velocities_out.append(127)
        else:
            register_note_count = len(self.note_register)
            if self.retrigger_mode == 2:
                midi_tick = self.transport.midi_tick
                self.note_register_position = int(midi_tick % (self.rate_to_midi_ticks()*register_note_count)/register_note_count)
            note = self.note_register[self.note_register_position % register_note_count]
            self.note_ons_out.append(note)
            self.velocities_out.append(127)
            self.note_register_position = (self.note_register_position+1) % register_note_count

        self.scheduled_offs.extend(self.note_ons_out)
        scheduled_list = [scheduled]*len(self.note_ons_out)
        self.scheduled_offs_time.extend(scheduled_list)

        #print("Note Register", self.note_register)
        #print("Note Register Position", self.note_register_position)

    def process_note_offs(self, force_all = False):
        current = ticks.ticks_ms()
        popped_ids = []
        if not force_all:
            for idx, note_off in enumerate(self.scheduled_offs):
                off_time = self.scheduled_offs_time[idx]
                if ticks.ticks_less(off_time, current):
                    self.note_offs_out.append(note_off)
                    popped_ids.append(idx)
        else: #Send note offs for all
            for idx, note_off in enumerate(self.scheduled_offs):
                self.note_offs_out.append(note_off)
                popped_ids.append(idx)

        for popped_id in reversed(popped_ids):
            self.scheduled_offs.pop(popped_id)
            self.scheduled_offs_time.pop(popped_id)

    def process(self, note_ons, note_offs, velocities):
        #TODO: Implement velocities in note_register and then be passed along
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.velocities_out.clear()
        running = self.transport.running
        if note_ons or note_offs:
            self.update_note_register(note_ons, note_offs, velocities)
        if self.arp_state == 1 and not self.note_register: #All keys released
            self.arp_state = 0
            #print("All Keys Released")
            if self.retrigger_mode == 0:
                self.note_register_position = 0
            self.update_random_seed()
        if self.arp_state == 0 and self.note_register: #From no keys pressed -> keys pressed
            self.arp_state = 1
            self.last_midi_tick = self.transport.midi_tick
            #print("Keys Pressed")
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
                if self.note_register:
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

        return self.note_ons_out, self.note_offs_out, self.velocities_out

    def stop(self):
        self.process_note_offs(force_all = True)
        self.held_notes.clear()







