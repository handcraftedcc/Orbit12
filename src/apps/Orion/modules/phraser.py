from ..core import music
from ..core import parms as Parms
from ..core import utils
from ..core.module import Module
from ..core.note_array import NoteOnArray, NoteOffArray


### PHRASER OPTIONS ###

PHRASE_HOLD = 0
PHRASE_RISE_DESCEND = 1
PHRASE_ARCH = 2
PHRASE_ORBIT = 3
PHRASE_FLARE = 4
PHRASE_JUMP = 5
PHRASE_SEQ = 6
PHRASE_HOME = 7
PHRASE_TYPES = (
    PHRASE_HOLD, PHRASE_RISE_DESCEND, PHRASE_ARCH,
    PHRASE_ORBIT, PHRASE_FLARE, PHRASE_JUMP, PHRASE_SEQ,
)
PHRASE_NAMES = ("HOLD", "RSEDSC", "ARCH", "ORBIT", "FLARE", "JUMP", "SEQ", "HOME")

RETRIGGER_OPTIONS = ("STBL", "CONT", "RTRG")
RETRIGGER_STABLE = 0
RETRIGGER_RETRIGGER = 2

QUANT_OPTIONS = ("NOTE", "PHR")
QUANT_NOTE = 0
QUANT_PHRASE = 1


def home_offset(last_offset, step, length):
    if last_offset == 0:
        return 0
    sign = 1 if last_offset > 0 else -1
    distance = abs(last_offset)
    progress = ((step + 1) * distance + length - 1) // length
    return sign * max(0, distance - progress)


def raw_phrase_weight(phrase_type, bank):
    return 20 + utils.random_int(bank * 1000 + phrase_type * 100 + 7000, 80, 0)


def phrase_weight(phrase_type, phrase_random_seed):
    bank = phrase_random_seed // 16
    mix = phrase_random_seed % 16
    low = raw_phrase_weight(phrase_type, bank)
    high = raw_phrase_weight(phrase_type, bank + 1)
    return (low * (16 - mix) + high * mix) // 16


def phrase_offset(phrase_type, step, length, direction):
    if phrase_type == PHRASE_HOLD:
        return 0
    if phrase_type == PHRASE_HOME:
        return 0
    if phrase_type == PHRASE_RISE_DESCEND:
        return direction * step
    if phrase_type == PHRASE_ARCH:
        if step <= length // 2:
            value = step + 1
        else:
            value = length - step + (1 if length % 2 == 0 else 0)
        return direction * value
    if phrase_type == PHRASE_ORBIT:
        turn = step % 4
        if turn == 1:
            return direction
        if turn == 3:
            return -direction
        return 0
    if phrase_type == PHRASE_FLARE:
        return direction if (step + 1) % 4 == 0 else 0
    if phrase_type == PHRASE_JUMP:
        if direction < 0:
            if step == 0:
                return 5
            if step <= 4:
                return step - 1
            return 2 + ((step - 5) % 2)
        if step == 0:
            return 0
        return 2 + abs(step - 4)
    if phrase_type == PHRASE_SEQ:
        value = step // 3 + step % 3
        if direction < 0:
            phrase_count = (length + 2) // 3
            return max(0, phrase_count + 1 - value)
        return value
    return 0


### PHRASER MODULE ###

class Phraser(Module):
    name = "phraser"
    label = "PHRSR"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.transport = module_helper.transport
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.len_phrase = 8
        self.len_pattern = 4
        self.up_down_bias = 0
        self.quant = QUANT_NOTE
        self.gate_max = 1
        self.seed = 0
        self.phrase_random_seed = 0
        self.retrigger_mode = RETRIGGER_STABLE

        self.home = 1

        self.held_notes = NoteOnArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()

        self.current_note = None
        self.current_velocity = 127
        self.pending_note = None
        self.pending_velocity = 127
        self.active_note = None
        self.current_offset = 0
        self.previous_phrase_offset = 0
        self.home_start_offset = 0
        self.current_phrase_index = -1
        self.gate_remaining = 0
        self.step_index = 0
        self.last_grid_bin = -1
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm(name="home", label="HOME", default=self.home,
                       parm_type=Parms.BooleanParmType,
                       bind_object=self, bind_attribute="home"),
            Parms.Parm(name="phrase_random_seed", label="PHRSD", default=self.phrase_random_seed,
                       parm_type=Parms.IntParmType, minmax=(0, 10000),
                       bind_object=self, bind_attribute="phrase_random_seed"),
            Parms.Parm(name="rate", label="RATE", default=self.rate_value,
                       parm_type=Parms.RateParmType, edit_callback_function=self.set_rate),
            Parms.Parm(name="len_phrase", label="LENPHR", default=self.len_phrase,
                       parm_type=Parms.IntParmType, minmax=(1, 64),
                       bind_object=self, bind_attribute="len_phrase"),
            Parms.Parm(name="len_pattern", label="LENPAT", default=self.len_pattern,
                       parm_type=Parms.IntParmType, minmax=(1, 64),
                       bind_object=self, bind_attribute="len_pattern"),
            Parms.Parm(name="up_down_bias", label="UPDNBS", default=self.up_down_bias,
                       parm_type=Parms.IntParmType, minmax=(-100, 100),
                       bind_object=self, bind_attribute="up_down_bias"),
            Parms.Parm(name="quant", label="QUANT", default=self.quant,
                       parm_type=Parms.EnumParmType, options=QUANT_OPTIONS,
                       bind_object=self, bind_attribute="quant"),
            Parms.Parm(name="gate_max", label="GATEMAX", default=self.gate_max,
                       parm_type=Parms.IntParmType, minmax=(1, 4),
                       bind_object=self, bind_attribute="gate_max"),
            Parms.Parm(name="seed", label="SEED", default=self.seed,
                       parm_type=Parms.IntParmType, minmax=(0, 10000),
                       bind_object=self, bind_attribute="seed"),
            Parms.Parm(name="retrigger_mode", label="RTRG", default=self.retrigger_mode,
                       parm_type=Parms.EnumParmType, options=RETRIGGER_OPTIONS,
                       bind_object=self, bind_attribute="retrigger_mode"),
        ]

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    ### SEEDS ###

    def phrase_step(self, step):
        return step % self.len_phrase

    def phrase_index(self, step):
        return (step // self.len_phrase) % self.len_pattern

    def phrase_seed(self, step):
        return self.seed + self.phrase_index(step)

    def step_seed(self, step):
        return self.seed + self.phrase_step(step)

    ### PHRASES ###

    def direction_for(self, step):
        chance_up = (self.up_down_bias + 100) / 200
        return 1 if utils.random_float(self.phrase_seed(step) + 1000) < chance_up else -1

    def phrase_type_for(self, step):
        if self.home and self.phrase_index(step) == self.len_pattern - 1:
            return PHRASE_HOME

        total = 0
        for phrase_type in PHRASE_TYPES:
            total += phrase_weight(phrase_type, self.phrase_random_seed)

        pick = utils.random_int(self.phrase_seed(step) + 2000, total - 1, 0)
        running = 0
        for phrase_type in PHRASE_TYPES:
            running += phrase_weight(phrase_type, self.phrase_random_seed)
            if pick < running:
                return phrase_type
        return PHRASE_HOLD

    def gate_steps_for(self, step):
        if self.gate_max <= 1:
            return 1
        return 1 + utils.random_int(self.step_seed(step) + 3000, self.gate_max - 1, 0)

    def global_root_note(self):
        return self.state.key + (self.state.octave + 2) * 12

    def global_offset_for(self, note):
        root = self.global_root_note()
        scale = music.SCALES[self.state.scale]
        normalized_note = (note - self.state.key + 12) % 12
        closest_index = 0
        closest_distance = abs(scale[0] - normalized_note)
        for index in range(1, len(scale)):
            distance = abs(scale[index] - normalized_note)
            if distance < closest_distance:
                closest_distance = distance
                closest_index = index
        return ((note - root) // 12) * len(scale) + closest_index

    ### INPUT STATE ###

    def queue_current_top_note(self):
        if self.held_notes.length == 0:
            self.pending_note = None
            return
        index = self.held_notes.length - 1
        self.pending_note = self.held_notes.notes[index]
        self.pending_velocity = self.held_notes.velocity_at(index)

    def update_held_notes(self, note_ons, note_offs):
        had_notes = self.held_notes.length > 0
        got_note_on = False

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            self.held_notes.append_value(note, velocity=note_ons.velocity_at(i))
            self.queue_current_top_note()
            got_note_on = True

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.held_notes.remove_value_first(note)
            if note == self.current_note or note == self.pending_note:
                self.queue_current_top_note()

        if got_note_on and not had_notes and self.retrigger_mode == RETRIGGER_RETRIGGER:
            self.step_index = 0
            self.last_grid_bin = -1

    def apply_pending_note(self, phrase_step):
        if self.pending_note is None:
            return False
        if self.current_note is None or self.quant == QUANT_NOTE or phrase_step == 0:
            changed = self.current_note != self.pending_note
            self.current_note = self.pending_note
            self.current_velocity = self.pending_velocity
            self.pending_note = None
            return changed
        return False

    ### NOTE GENERATION ###

    def update_phrase_boundary(self, step):
        phrase_index = self.phrase_index(step)
        if phrase_index == self.current_phrase_index:
            return
        if self.current_phrase_index >= 0:
            self.previous_phrase_offset = self.current_offset
        self.current_phrase_index = phrase_index
        self.home_start_offset = self.previous_phrase_offset
        print("PHRSR", PHRASE_NAMES[self.phrase_type_for(step)])

    def stop_active_note(self):
        if self.active_note is not None:
            self.note_offs_out.append_value(self.active_note)
            self.active_note = None

    def generate_step(self, step):
        phrase_step = self.phrase_step(step)
        self.update_phrase_boundary(step)
        if self.apply_pending_note(phrase_step):
            self.gate_remaining = 0
        if self.current_note is None:
            return

        phrase_type = self.phrase_type_for(step)
        is_home = phrase_type == PHRASE_HOME
        if is_home:
            offset = home_offset(self.home_start_offset, phrase_step, self.len_phrase)
        else:
            offset = phrase_offset(phrase_type, phrase_step, self.len_phrase, self.direction_for(step))
        if offset is None:
            self.stop_active_note()
            self.gate_remaining = 0
            return

        if self.gate_remaining > 0:
            self.gate_remaining -= 1
            return

        scale = music.SCALES[self.state.scale]
        root_note = self.global_root_note() if is_home else self.current_note
        note = music.transpose(root_note, offset, 0, True, self.state.key, scale)
        self.stop_active_note()
        self.note_ons_out.append_value(note, velocity=self.current_velocity)
        self.active_note = note
        self.current_offset = self.global_offset_for(note)
        self.gate_remaining = self.gate_steps_for(step) - 1

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.update_held_notes(note_ons, note_offs)

        if self.held_notes.length == 0:
            self.current_note = None
            self.pending_note = None
            self.stop_active_note()
            return self.note_ons_out, self.note_offs_out

        running = self.transport.running
        if running and not self.was_transport_running:
            self.last_grid_bin = -1
            if self.retrigger_mode == RETRIGGER_RETRIGGER:
                self.step_index = 0

        if running:
            current_bin = self.transport.midi_tick // self.rate_ticks
            if current_bin < self.last_grid_bin:
                self.last_grid_bin = -1
            if current_bin != self.last_grid_bin:
                self.last_grid_bin = current_bin
                if self.retrigger_mode == RETRIGGER_STABLE:
                    step = current_bin
                else:
                    step = self.step_index
                    self.step_index += 1
                self.generate_step(step)

        self.was_transport_running = running
        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes.clear()
        self.current_note = None
        self.pending_note = None
        self.active_note = None
        self.current_offset = 0
        self.previous_phrase_offset = 0
        self.home_start_offset = 0
        self.current_phrase_index = -1
        self.gate_remaining = 0
        self.step_index = 0
        self.last_grid_bin = -1

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
