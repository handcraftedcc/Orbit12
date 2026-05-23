from ..core import music
from ..core.constants import POLYPHONY
from ..core.module import Module
from ..core import parms as Parms
import adafruit_ticks as ticks
import random
from ..core import utils
from ..core.note_array import NoteArray,NoteOnArray,NoteOffArray

MODE_LIST = [
    "UP",
    "DOWN",
    "RND",
    "ORD",
    "RPT",
]

MODE_UP = MODE_LIST.index("UP")
MODE_DOWN = MODE_LIST.index("DOWN")
MODE_RANDOM = MODE_LIST.index("RND")
MODE_INORDER = MODE_LIST.index("ORD")
MODE_REPEAT = MODE_LIST.index("RPT")

class Arp(Module):
    name = "arp"
    label = "ARP"
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
        self.retrigger_mode = 0
        self.retrigger_mode_list = [
            "RTRG",
            "CONT",
            "STBL",
        ]
        self.gate = 0
        self.gate_random = 0
        self.arp_state = 0 #0 = Not active, 1 = Active -> When all notes are released we go empty
        self.random_seed_user = 0
        self.random_pattern_length = 0
        self.selected_pattern = 0
        self.active_pattern = music.patterns_bit[self.selected_pattern]
        self.active_pattern_length = music.patterns_len[self.selected_pattern]
        self.active_pattern_step = 0
        self.pattern_shift = 0

        self.held_notes = NoteArray()

        self.last_midi_tick = self.transport.midi_tick
        self.last_grid_bin = -1
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

        # Init values
        self.rate = self.rate_parm.get_actual_value()
        self.mode = self.mode_parm.get_actual_value()
        self.gate = self.gate_parm.get_actual_value()

    def create_main_parms(self):
        parms = []
        # Rate
        self.rate_parm = Parms.Parm(name="rate", label="RATE", default=8, parm_type=Parms.RateParmType,
                                    edit_callback_function=self.set_rate)
        parms.append(self.rate_parm)

        # Mode
        self.mode_parm = Parms.Parm(name="mode", label="MDE", default=0, parm_type=Parms.EnumParmType,
                                    options=MODE_LIST,edit_callback_function=self.set_mode)
        parms.append(self.mode_parm)

        # Gate
        self.gate_parm = Parms.Parm(name="gate", label="GATE", default=100, parm_type=Parms.FloatParmType,
                                    increment=5, edit_callback_function=self.set_gate)
        parms.append(self.gate_parm)

        # Pattern
        self.pattern_parm = Parms.Parm(name="pattern", label="PTN", default=self.selected_pattern,
                                           parm_type=Parms.EnumParmType, options=music.patterns_text,
                                           edit_callback_function=self.set_pattern)
        parms.append(self.pattern_parm)

        self.pattern_shift_parm = Parms.Parm(name="pattern_shift", label="PTN SHFT", default=self.pattern_shift,
                                       parm_type=Parms.IntParmType, edit_callback_function=self.set_pattern_shift)
        parms.append(self.pattern_shift_parm)

        # Gate Randomize
        self.gate_random_parm = Parms.Parm(name="gate_random", label="RND GATE", default=0,
                                           parm_type=Parms.IntParmType,
                                           increment=5, edit_callback_function=self.set_gate_random)
        parms.append(self.gate_random_parm)

        # Random Pattern Length
        self.random_pattern_length_parm = Parms.Parm(name="random_pattern_length", label="RND LEN", default=0,
                                                    parm_type=Parms.IntParmType,minmax = (0,128),
                                                    edit_callback_function=self.set_random_pattern_length)
        parms.append(self.random_pattern_length_parm)

        # Random Seed
        self.random_seed_parm = Parms.Parm(name="rand_seed", label="SEED", default=0,
                                           parm_type=Parms.IntParmType,
                                           minmax=(0, 10000), edit_callback_function=self.set_random_seed_user)
        parms.append(self.random_seed_parm)

        # Retrigger Mode
        self.retrigger_parm = Parms.Parm(name="retrigger_mode", label="RTRG", default=0,
                                         parm_type=Parms.EnumParmType,
                                         options=self.retrigger_mode_list,
                                         edit_callback_function=self.set_retrigger_mode)
        parms.append(self.retrigger_parm)

        return parms

    def set_rate(self, value):
        self.rate = self.rate_parm.get_actual_value()
        return self.rate

    def set_mode(self, value):
        self.mode = self.mode_parm.get_actual_value()
        return self.mode

    def set_gate(self, value):
        self.gate = value
        return self.gate

    def set_gate_random(self, value):
        self.gate_random = value
        return self.gate_random

    def set_pattern(self, value):
        self.selected_pattern = value
        self.active_pattern = music.patterns_bit[self.selected_pattern]
        self.active_pattern_length = music.patterns_len[self.selected_pattern]
        return self.selected_pattern

    def set_pattern_shift(self, value):
        self.pattern_shift = value
        return self.pattern_shift

    def set_retrigger_mode(self, value):
        self.retrigger_mode = value
        return self.retrigger_mode

    def set_random_seed_user(self, value):
        self.random_seed_user = value
        return self.random_seed_user

    def set_random_pattern_length(self, value):
        self.random_pattern_length = value
        return self.random_pattern_length

    def rate_to_midi_ticks(self):
        # RATE_VALUES are in 1/16-note units; 1/16 = 6 MIDI clock ticks
        return max(1, int(round(self.rate * 6)))

    def update_note_register(self, note_ons, note_offs):
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            velocity = 127
            if note_ons.velocities is not None: velocity = note_ons.velocities[i]
            self.held_notes.append_value(note)
            if not self.note_register.contains(note):
                self.note_register.append_value(note, velocity=velocity)
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            if self.note_register.contains(note) and not self.held_notes.contains(note):
                self.note_register.remove_value_first(note)
        if self.mode == MODE_UP or self.mode == MODE_RANDOM:
            self.note_register.sort_notes()
        elif self.mode == MODE_DOWN:
            self.note_register.sort_notes()
            self.note_register.reverse_notes()

    def generate_notes(self):
        #Calculate timing
        current = self.transport.now
        scheduled = ticks.ticks_add(current, self.gate)
        midi_tick = self.transport.midi_tick
        timing_tick = midi_tick // (self.rate_to_midi_ticks())
        rand_seed_time_temp = timing_tick
        if self.random_pattern_length != 0:
            rand_seed_time_temp = timing_tick % self.random_pattern_length

        if self.retrigger_mode == 2: #Stable mode
                self.active_pattern_step = timing_tick

        check_pattern = music.get_pattern_step(self.active_pattern,self.active_pattern_length,
                                           self.active_pattern_step + self.pattern_shift)
        self.active_pattern_step += 1
        if check_pattern == 0: #If pattern is 0 on this step, don't emit notes
            return

        if self.mode == MODE_REPEAT: #Repeat mode
            for i in range(self.note_register.length):
                note = self.note_register.notes[i]
                if self.note_register.velocities:
                    velocity = self.note_register.velocities[i]
                else:
                    velocity = 127
                random_gate = self.gate + utils.random_int(self.random_seed_user + rand_seed_time_temp + i, self.gate_random, 0)
                self.note_ons_out.append_value(note,velocity=velocity)
                self.scheduled_offs.append_value(note, time=scheduled + random_gate)

        else:
            register_note_count = self.note_register.length
            if self.retrigger_mode == 2:  # Stable steps
                pattern_len = self.active_pattern_length
                current_pattern_iteration = timing_tick // pattern_len
                current_pattern_step = timing_tick % pattern_len

                empty_steps_per_pattern = music.get_pattern_empty_steps(
                    self.active_pattern,
                    pattern_len
                )
                active_steps_per_pattern = pattern_len - empty_steps_per_pattern

                active_steps_past = current_pattern_iteration * active_steps_per_pattern

                for i in range(current_pattern_step):
                    pattern_step = music.get_pattern_step(
                        self.active_pattern,
                        pattern_len,
                        i + self.pattern_shift
                    )

                    if pattern_step:
                        active_steps_past += 1

                self.note_register_position = active_steps_past

            if self.mode == MODE_RANDOM:
                index = utils.random_int(rand_seed_time_temp + self.random_seed_user, register_note_count-1, 0)
            else:
                index = self.note_register_position % register_note_count
            note = self.note_register.notes[index]
            if self.note_register.velocities:
                velocity = self.note_register.velocities[index]
            else:
                velocity = 127
            random_gate = self.gate+utils.random_int(rand_seed_time_temp + self.random_seed_user,self.gate_random,0)
            self.note_ons_out.append_value(note, velocity=velocity)
            self.scheduled_offs.append_value(note, time=scheduled+random_gate)
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
                self.active_pattern_step = 0

        if self.arp_state == 0 and self.note_register.length>0: #From no keys pressed -> keys pressed
            self.arp_state = 1

        if running and not self.was_transport_running: #Transport started
            self.last_grid_bin = -1
            if self.retrigger_mode == 0:
                self.note_register_position = 0
                self.active_pattern_step = 0

        if self.transport.running:
            interval = self.rate_to_midi_ticks()  # e.g. 6 for 1/16
            current_bin = self.transport.midi_tick // interval

            if current_bin < self.last_grid_bin:
                self.last_grid_bin = -1

            if current_bin != self.last_grid_bin:
                previous_grid_bin = self.last_grid_bin
                self.last_grid_bin = current_bin

                if self.arp_state == 1:
                    if previous_grid_bin < 0:
                        triggers = 1
                    else:
                        triggers = current_bin - previous_grid_bin

                    for _ in range(triggers):
                        self.generate_notes()

        self.process_note_offs()

        self.was_transport_running = running

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.scheduled_offs.clear()
        self.popped_ids.clear()
        self.note_register.clear()
        self.held_notes.clear()
        self.arp_state = 0
        self.note_register_position = 0
        self.active_pattern_step = 0
        self.last_grid_bin = -1
        self.was_transport_running = False


