from ..core import music, parms as Parms, utils
from ..core.module import Module
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks
import gc

gc.collect()

### ARPWALK OPTIONS ###

SORT_LIST = ("UP", "DOWN", "ORD")
SORT_UP, SORT_DOWN, SORT_INORDER = 0, 1, 2
MAX_STEPS = 8
RANDOM_STEP_MAX = 3
DEFAULT_STEP_VALUES = (-1, 2, 0, 0, 0, 0, 0, 0)


### ARPWALK MODULE ###

class ArpWalk(Module):
    name = "arp_walk"
    label = "ARPWLK"
    version = 1
    save_attrs = ("step_values",)

    def __init__(self, module_helper, slot_id):
        self.transport = module_helper.transport
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.note_register = NoteOnArray()
        self.held_notes = NoteArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_offs = NoteArray(times=True)
        self.popped_ids = NoteArray()
        self.sort_mode = SORT_UP
        self.step_length = 2
        self.step_select = 0
        self.step_values = list(DEFAULT_STEP_VALUES)
        self.random_counter = 0
        self.invert = 0
        self.max_walk = 0
        self.boundary_mode = utils.BOUNDARY_WRAP_UNI
        self.gate = 100
        self.retrigger_mode = 0
        self.retrigger_mode_list = ("RTRG", "CONT", "STBL")
        self.selected_pattern = 0
        self.active_pattern = music.patterns_bit[self.selected_pattern]
        self.active_pattern_length = music.patterns_len[self.selected_pattern]
        self.active_pattern_step = 0
        self.pattern_shift = 0
        self.arp_state = 0
        self.walk_position = 0
        self.step_position = 0
        self.last_grid_bin = -1
        self.was_transport_running = self.transport.running
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm("rate", "RATE", Parms.RateParmType, self.rate_value, edit_callback_function=self.set_rate),
            Parms.Parm("sort_mode", "SORT", Parms.EnumParmType, self.sort_mode,
                       options=SORT_LIST, bind_object=self, bind_attribute="sort_mode"),
            Parms.Parm("step_length", "LEN", Parms.IntParmType, self.step_length,
                       minmax=(1, MAX_STEPS), edit_callback_function=self.set_step_length),
            Parms.Parm("step_select", "SEL", Parms.IntParmType, self.step_select,
                       minmax=(0, self.step_length - 1), edit_callback_function=self.set_step_select),
            Parms.Parm("step_value", "VAL", Parms.IntParmType, self.step_values[self.step_select],
                       minmax=(-24, 24), edit_callback_function=self.set_step_value),
            Parms.Parm("randomize_steps", "RAND", Parms.ButtonParmType, 0,
                       enter_callback_function=self.randomize_steps),
            Parms.Parm("invert", "INV", Parms.BooleanParmType, self.invert,
                       bind_object=self, bind_attribute="invert"),
            Parms.Parm("max_walk", "MAXWLK", Parms.IntParmType, self.max_walk,
                       minmax=(0, 48), bind_object=self, bind_attribute="max_walk"),
            Parms.Parm("boundary_mode", "BND", Parms.EnumParmType, self.boundary_mode,
                       options=utils.BOUNDARY_LIST, bind_object=self, bind_attribute="boundary_mode"),
            Parms.Parm("gate", "GATE", Parms.FloatParmType, self.gate,
                       increment=5, minmax=(0,100000), bind_object=self, bind_attribute="gate"),
            Parms.Parm("rhythm_pattern", "RYTM", Parms.PatternParmType, self.selected_pattern,
                       edit_callback_function=self.set_pattern),
            Parms.Parm("pattern_shift", "RYSHFT", Parms.IntParmType, self.pattern_shift,
                       bind_object=self, bind_attribute="pattern_shift"),
            Parms.Parm("retrigger_mode", "RTRG", Parms.EnumParmType, self.retrigger_mode,
                       options=self.retrigger_mode_list, bind_object=self, bind_attribute="retrigger_mode"),
        ]

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def set_pattern(self, value):
        self.selected_pattern = value
        self.active_pattern = music.patterns_bit[value]
        self.active_pattern_length = music.patterns_len[value]
        return value

    def rate_to_midi_ticks(self):
        return self.rate_ticks

    ### STEP PATTERN ###

    def sync_step_parms(self):
        select_parm = self.get_parm_by_name("step_select")
        if select_parm is not None:
            select_parm.minmax = (0, self.step_length - 1)
            select_parm.set_value(self.step_select)
        value_parm = self.get_parm_by_name("step_value")
        if value_parm is not None:
            value_parm.set_value(self.step_values[self.step_select])

    def set_step_length(self, value):
        self.step_length = max(1, min(MAX_STEPS, value))
        if self.step_select >= self.step_length:
            self.step_select = self.step_length - 1
        self.sync_step_parms()
        self.queue_parm_rebuild()
        return self.step_length

    def set_step_select(self, value):
        self.step_select = max(0, min(self.step_length - 1, value))
        self.sync_step_parms()
        self.queue_parm_rebuild()
        return self.step_select

    def set_step_value(self, value):
        self.step_values[self.step_select] = value
        return value

    def randomize_steps(self, value):
        self.random_counter += 1
        seed = self.random_counter + self.transport.midi_tick
        for i in range(self.step_length):
            self.step_values[i] = utils.random_int(seed + i, RANDOM_STEP_MAX, 1)
        self.sync_step_parms()
        self.queue_parm_rebuild()
        return value

    ### NOTE REGISTER ###

    def update_note_register(self, note_ons, note_offs):
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            self.held_notes.append_value(note)
            if not self.note_register.contains(note):
                self.note_register.append_value(note, velocity=note_ons.velocity_at(i))
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            if self.note_register.contains(note) and not self.held_notes.contains(note):
                self.note_register.remove_value_first(note)
        if self.sort_mode == SORT_UP:
            self.note_register.sort_notes()
        elif self.sort_mode == SORT_DOWN:
            self.note_register.sort_notes()
            self.note_register.reverse_notes()

    ### WALK NOTES ###

    def apply_step(self):
        step = self.step_values[self.step_position % self.step_length]
        self.walk_position += -step if self.invert else step
        self.step_position += 1

    def bounded_walk_position(self):
        if self.invert: return -utils.bound_offset(-self.walk_position, self.max_walk, self.boundary_mode)
        return utils.bound_offset(self.walk_position, self.max_walk, self.boundary_mode)

    def note_for_single(self):
        scale = music.SCALES[self.state.scale]
        root = self.note_register.notes[0]
        while True:
            note = music.transpose(root, self.bounded_walk_position(), 0, True, self.state.key, scale)
            if note > 127:
                self.walk_position -= 1
            elif note < 0:
                self.walk_position += 1
            else:
                return note, self.note_register.velocity_at(0)

    def note_for_chord(self):
        count = self.note_register.length
        while True:
            position = self.bounded_walk_position()
            index = position % count
            octave = position // count
            note = self.note_register.notes[index] + octave * 12
            if note > 127:
                self.walk_position -= 1
            elif note < 0:
                self.walk_position += 1
            else:
                return note, self.note_register.velocity_at(index)

    ### NOTE GENERATION ###

    def generate_notes(self):
        if self.note_register.length == 0:
            return
        current = self.transport.now
        midi_tick = self.transport.midi_tick
        timing_tick = midi_tick // self.rate_to_midi_ticks()
        if self.retrigger_mode == 2:
            self.active_pattern_step = timing_tick

        check_pattern = music.get_pattern_step(self.active_pattern, self.active_pattern_length,
                                               self.active_pattern_step + self.pattern_shift)
        self.active_pattern_step += 1
        if check_pattern == 0:
            return

        self.apply_step()
        note, velocity = self.note_for_single() if self.note_register.length == 1 else self.note_for_chord()
        self.note_ons_out.append_value(note, velocity=velocity)
        self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, int(self.gate)))

    ### SCHEDULED OFFS ###

    def process_note_offs(self, force_all=False):
        current = self.transport.now
        if not force_all:
            self.scheduled_offs.pop_due(current, self.note_offs_out, self.popped_ids)
            return
        self.popped_ids.clear()
        for i in range(self.scheduled_offs.length):
            self.note_offs_out.append_value(self.scheduled_offs.notes[i])
            self.popped_ids.append_value(i)
        self.scheduled_offs.remove_indexes(self.popped_ids)

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        running = self.transport.running

        #Check if note offs temporarily turn off all notes
        counter = 0
        for i in range(note_offs.length):
            if self.note_register.contains(note_offs.notes[i]):
                counter += 1

        same_frame_retrigger = False
        if counter == self.note_register.length:
            same_frame_retrigger = True

        if note_ons.length > 0 or note_offs.length > 0:
            self.update_note_register(note_ons, note_offs)

        if (self.arp_state == 1 and self.note_register.length < 1) or same_frame_retrigger:
            self.arp_state = 0
            if self.retrigger_mode == 0:
                self.walk_position = 0
                self.step_position = 0
                self.active_pattern_step = 0

        if self.arp_state == 0 and self.note_register.length > 0:
            self.arp_state = 1

        if running and not self.was_transport_running:
            self.last_grid_bin = -1
            if self.retrigger_mode == 0:
                self.walk_position = 0
                self.step_position = 0
                self.active_pattern_step = 0

        if running:
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
        self.walk_position = 0
        self.step_position = 0
        self.active_pattern_step = 0
        self.last_grid_bin = -1
        self.was_transport_running = False

    def remove(self):
        super().remove()
        self.transport = None
        self.note_register = None
        self.held_notes = None
        self.scheduled_offs = None
        self.popped_ids = None
        self.retrigger_mode_list = None
