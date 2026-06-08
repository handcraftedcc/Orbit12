from ..core import music, parms as Parms
from ..core.module import Module
import adafruit_ticks as ticks
from ..core import utils
from ..core.note_array import NoteArray,NoteOnArray,NoteOffArray
import gc

gc.collect()

### ARP MODES ###

SORT_LIST = ("UP", "DOWN", "ORD")
SORT_UP, SORT_DOWN, SORT_INORDER = 0, 1, 2

PLAY_LIST = ("SORT", "MIRR", "THUMB", "PINKY", "DIV", "CONV", "CDIV", "RND", "RPT")
MODE_LIST = ("UP", "DOWN", "RND", "ORD", "RPT")

PLAY_SORT, PLAY_MIRROR, PLAY_THUMB, PLAY_PINKY, PLAY_DIVERGE = 0, 1, 2, 3, 4
PLAY_CONVERGE, PLAY_CONDIVERGE, PLAY_RANDOM, PLAY_REPEAT = 5, 6, 7, 8

# Backwards-compatible names for old imports.
MODE_UP = SORT_UP
MODE_DOWN = SORT_DOWN
MODE_INORDER = SORT_INORDER
MODE_RANDOM = PLAY_RANDOM
MODE_REPEAT = PLAY_REPEAT

### ARP MODULE ###

class Arp(Module):
    name = "arp"
    label = "ARP"
    version = 1
    def __init__(self, module_helper, slot_id):
        #TODO: Rewrite to new Note Array Format
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        # Setup Attribs
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.note_register = NoteOnArray()
        self.note_register_position = 0
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_offs = NoteArray(times = True)
        self.popped_ids = NoteArray()
        self.sort_mode = SORT_UP
        self.play_mode = PLAY_SORT
        self.transpose_steps = 0
        self.max_transpose = 0
        self.boundary_mode = utils.BOUNDARY_WRAP_UNI
        self.retrigger_mode = 0
        self.retrigger_mode_list = ("RTRG", "CONT", "STBL")
        self.gate = 100
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

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm("rate", "RATE", Parms.RateParmType, self.rate_value, edit_callback_function=self.set_rate),
            Parms.Parm("sort_mode", "SORT", Parms.EnumParmType, self.sort_mode,
                       options=SORT_LIST, bind_object=self, bind_attribute="sort_mode"),
            Parms.Parm("play_mode", "PLAY", Parms.EnumParmType, self.play_mode,
                       options=PLAY_LIST, bind_object=self, bind_attribute="play_mode"),
            Parms.Parm("transpose_steps", "TRNS", Parms.IntParmType, self.transpose_steps,
                       minmax=(-24, 24), bind_object=self, bind_attribute="transpose_steps"),
            Parms.Parm("max_transpose", "MAXTRN", Parms.IntParmType, self.max_transpose,
                       minmax=(0, 48), bind_object=self, bind_attribute="max_transpose"),
            Parms.Parm("boundary_mode", "BND", Parms.EnumParmType, self.boundary_mode,
                       options=utils.BOUNDARY_LIST, bind_object=self, bind_attribute="boundary_mode"),
            Parms.Parm("gate", "GATE", Parms.FloatParmType, self.gate,
                       increment=5, minmax=(0,100000), bind_object=self, bind_attribute="gate"),
            Parms.Parm("pattern", "PTN", Parms.PatternParmType, self.selected_pattern,
                       edit_callback_function=self.set_pattern),
            Parms.Parm("pattern_shift", "PTSHFT", Parms.IntParmType, self.pattern_shift,
                       bind_object=self, bind_attribute="pattern_shift"),
            Parms.Parm("random_pattern_length", "RNDLEN", Parms.IntParmType, self.random_pattern_length,
                       minmax=(0,128), bind_object=self, bind_attribute="random_pattern_length"),
            Parms.Parm("rand_seed", "SEED", Parms.IntParmType, self.random_seed_user,
                       minmax=(0, 10000), bind_object=self, bind_attribute="random_seed_user"),
            Parms.Parm("retrigger_mode", "RTRG", Parms.EnumParmType, self.retrigger_mode,
                       options=self.retrigger_mode_list, bind_object=self, bind_attribute="retrigger_mode"),
        ]

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def set_pattern(self, value):
        self.selected_pattern = value
        self.active_pattern = music.patterns_bit[self.selected_pattern]
        self.active_pattern_length = music.patterns_len[self.selected_pattern]
        return self.selected_pattern

    def rate_to_midi_ticks(self):
        return self.rate_ticks

    ### NOTE REGISTER ###

    def update_note_register(self, note_ons, note_offs):
        # Track physical holds separately so duplicate notes release correctly.
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            velocity = note_ons.velocity_at(i)
            self.held_notes.append_value(note)
            if not self.note_register.contains(note):
                self.note_register.append_value(note, velocity=velocity)
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            if self.note_register.contains(note) and not self.held_notes.contains(note):
                self.note_register.remove_value_first(note)
        if self.sort_mode == SORT_UP or self.play_mode == PLAY_RANDOM:
            self.note_register.sort_notes()
        elif self.sort_mode == SORT_DOWN:
            self.note_register.sort_notes()
            self.note_register.reverse_notes()

    ### PLAY PATHS ###

    def diverge_index(self, position, count):
        center = (count - 1) // 2
        if position == 0:
            return center
        step = (position + 1) // 2
        if position % 2:
            return center + step
        return center - step

    def path_length(self, count):
        if count < 2:
            return 1
        if self.play_mode in (PLAY_MIRROR, PLAY_THUMB, PLAY_PINKY):
            return count * 2 - 2
        if self.play_mode == PLAY_CONDIVERGE:
            return count * 2 - 1
        return count

    def path_index(self, position, count):
        if count < 2:
            return 0
        length = self.path_length(count)
        position = position % length
        mode = self.play_mode
        if mode == PLAY_MIRROR:
            return position if position < count else length - position
        if mode == PLAY_THUMB:
            return 0 if position % 2 == 0 else (position + 1) // 2
        if mode == PLAY_PINKY:
            return count - 1 if position % 2 == 0 else (position - 1) // 2
        if mode == PLAY_DIVERGE:
            return self.diverge_index(position, count)
        if mode == PLAY_CONVERGE:
            return self.diverge_index(count - 1 - position, count)
        if mode == PLAY_CONDIVERGE:
            if position < count: return self.diverge_index(count - 1 - position, count)
            return self.diverge_index(position - count + 1, count)
        return position % count

    def transpose_note(self, note, completion):
        steps = self.transpose_steps * completion
        steps = utils.bound_offset(steps, self.max_transpose, self.boundary_mode)
        if steps:
            scale = music.SCALES[self.state.scale]
            note = music.transpose(note, steps, 0, True, self.state.key, scale)
        return max(0, min(127, note))

    ### NOTE GENERATION ###

    def generate_notes(self):
        current = self.transport.now
        gate = int(self.gate)
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

        if self.play_mode == PLAY_REPEAT: #Repeat mode
            # Repeat mode emits every held note on each active pattern step.
            completion = self.note_register_position
            for i in range(self.note_register.length):
                note = self.transpose_note(self.note_register.notes[i], completion)
                velocity = self.note_register.velocity_at(i)
                self.note_ons_out.append_value(note,velocity=velocity)
                self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, gate))
            self.note_register_position += 1

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

            path_len = self.path_length(register_note_count)
            completion = self.note_register_position // path_len
            if self.play_mode == PLAY_RANDOM:
                index = utils.random_int(rand_seed_time_temp + self.random_seed_user, register_note_count-1, 0)
            else:
                index = self.path_index(self.note_register_position, register_note_count)
            # Non-repeat modes emit one selected note per active pattern step.
            note = self.transpose_note(self.note_register.notes[index], completion)
            velocity = self.note_register.velocity_at(index)
            self.note_ons_out.append_value(note, velocity=velocity)
            self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, gate))
            self.note_register_position += 1

        #print("Note Register", self.note_register.notes)
        #print("Note Register Position", self.note_register_position)

    ### SCHEDULED OFFS ###

    def process_note_offs(self, force_all = False):
        #TODO: Maybe build a time removal thing into the note_array directly
        current = self.transport.now
        if not force_all:
            self.scheduled_offs.pop_due(current, self.note_offs_out, self.popped_ids)
            return
        else: #Send note offs for all
            self.popped_ids.clear()
            for i in range(self.scheduled_offs.length):
                note_off = self.scheduled_offs.notes[i]
                self.note_offs_out.append_value(note_off)
                self.popped_ids.append_value(i)

        self.scheduled_offs.remove_indexes(self.popped_ids)

    ### PROCESSING ###

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
            self.last_grid_bin, changed, triggers = utils.grid_change(
                self.transport.midi_tick, self.rate_to_midi_ticks(), self.last_grid_bin
            )
            if changed and self.arp_state == 1:
                for _ in range(triggers):
                    self.generate_notes()

        self.process_note_offs()

        self.was_transport_running = running

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

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

    def remove(self):
        super().remove()
        self.transport = None
        self.note_register = None
        self.scheduled_offs = None
        self.popped_ids = None
        self.held_notes = None
        self.retrigger_mode_list = None
