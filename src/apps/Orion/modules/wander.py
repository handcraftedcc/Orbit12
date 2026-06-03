from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core import utils
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks


RETRIGGER_OPTIONS = (
    "STBL",
    "CONT",
    "RTRG",
)

RETRIGGER_STABLE = 0
RETRIGGER_RETRIGGER = 2


class Wander(Module):
    name = "wander"
    label = "WANDR"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.gravity = 50
        self.min_step = 1
        self.max_step = 2
        self.max_deviation = 8
        self.tension = 0
        self.random_seed_user = 0
        self.loop_length = 0
        self.retrigger_mode = RETRIGGER_STABLE
        self.selected_pattern = 0
        self.active_pattern = music.patterns_bit[self.selected_pattern]
        self.active_pattern_length = music.patterns_len[self.selected_pattern]
        self.gate = 100

        self.held_notes = NoteArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()

        self.center_note = 60
        self.current_note = 60
        self.step_index = 0
        self.last_grid_bin = -1
        self.last_velocity = 127
        self.active_note = None
        self.active_off_time = 0
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []

        parms.append(Parms.Parm(
            name="rate", label="RATE", default=self.rate_value,
            parm_type=Parms.RateParmType,
            edit_callback_function=self.set_rate
        ))

        parms.append(Parms.Parm(
            name="gravity", label="GRAV", default=self.gravity,
            parm_type=Parms.IntParmType,
            minmax=(0, 100),
            bind_object=self,
            bind_attribute="gravity"
        ))

        parms.append(Parms.Parm(
            name="min_step", label="MIN", default=self.min_step,
            parm_type=Parms.IntParmType,
            minmax=(0, 16),
            bind_object=self,
            bind_attribute="min_step"
        ))

        parms.append(Parms.Parm(
            name="max_step", label="MAX", default=self.max_step,
            parm_type=Parms.IntParmType,
            minmax=(0, 16),
            bind_object=self,
            bind_attribute="max_step"
        ))

        parms.append(Parms.Parm(
            name="max_deviation", label="DEV", default=self.max_deviation,
            parm_type=Parms.IntParmType,
            minmax=(0, 24),
            bind_object=self,
            bind_attribute="max_deviation"
        ))

        parms.append(Parms.Parm(
            name="tension", label="TENS", default=self.tension,
            parm_type=Parms.IntParmType,
            minmax=(0, 100),
            bind_object=self,
            bind_attribute="tension"
        ))

        parms.append(Parms.Parm(
            name="rand_seed", label="SEED", default=self.random_seed_user,
            parm_type=Parms.IntParmType,
            minmax=(0, 10000),
            bind_object=self,
            bind_attribute="random_seed_user"
        ))

        parms.append(Parms.Parm(
            name="loop_length", label="LOOP", default=self.loop_length,
            parm_type=Parms.IntParmType,
            minmax=(0, 128),
            bind_object=self,
            bind_attribute="loop_length"
        ))

        parms.append(Parms.Parm(
            name="retrigger_mode", label="RTRG", default=self.retrigger_mode,
            parm_type=Parms.EnumParmType,
            options=RETRIGGER_OPTIONS,
            bind_object=self,
            bind_attribute="retrigger_mode"
        ))

        parms.append(Parms.Parm(
            name="rhythm", label="RTHM", default=self.selected_pattern,
            parm_type=Parms.PatternParmType,
            edit_callback_function=self.set_pattern
        ))

        parms.append(Parms.Parm(
            name="gate", label="GATE", default=self.gate,
            parm_type=Parms.FloatParmType,
            increment=5,
            bind_object=self,
            bind_attribute="gate"
        ))

        return parms

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def set_pattern(self, value):
        self.selected_pattern = value
        self.active_pattern = music.patterns_bit[value]
        self.active_pattern_length = music.patterns_len[value]
        return value

    def clamp_note(self, note):
        if note < 0:
            return 0
        if note > 127:
            return 127
        return note

    def reset_walk(self):
        self.current_note = self.center_note
        self.step_index = 0
        self.last_grid_bin = -1

    def transpose_current_note(self, steps):
        scale = music.SCALES[self.module_helper.state.scale]
        note = music.transpose(self.current_note, steps, 0, True, self.state.key, scale)
        return self.clamp_note(note)

    def update_held_notes(self, note_ons, note_offs):
        got_note_on = False

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            had_notes = self.held_notes.length > 0

            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)

            if self.held_notes.append_value(note):
                self.center_note = note
                if not had_notes:
                    self.current_note = note
                got_note_on = True

            if note_ons.velocities is not None:
                self.last_velocity = note_ons.velocities[i]
            else:
                self.last_velocity = 127

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.held_notes.remove_value_first(note)

        if self.held_notes.length > 0 and not self.held_notes.contains(self.center_note):
            self.center_note = self.held_notes.notes[self.held_notes.length - 1]

        if got_note_on and self.retrigger_mode == RETRIGGER_RETRIGGER:
            self.step_index = 0
            self.last_grid_bin = -1

        return got_note_on

    def process_active_note_off(self, force=False):
        if self.active_note is None:
            return
        if force or ticks.ticks_less(self.active_off_time, self.transport.now):
            self.note_offs_out.append_value(self.active_note)
            self.active_note = None

    def generate_note(self, timing_step):
        if self.retrigger_mode == RETRIGGER_STABLE:
            step = timing_step
        else:
            step = self.step_index
            self.step_index += 1

        loop_step = step
        if self.loop_length > 0:
            loop_step = step % self.loop_length
            if loop_step == 0:
                self.current_note = self.center_note

        if not music.get_pattern_step(self.active_pattern, self.active_pattern_length, loop_step):
            return

        seed = self.random_seed_user + loop_step
        max_deviation = self.max_deviation
        direction = 1

        if max_deviation <= 0:
            self.current_note = self.center_note
        else:
            distance = self.current_note - self.center_note
            if abs(distance) >= max_deviation:
                direction = -1 if distance > 0 else 1
            elif distance != 0 and utils.random_int(seed, 99, 0) < self.gravity:
                direction = -1 if distance > 0 else 1
            elif utils.random_int(seed + 1, 1, 0) == 0:
                direction = -1

            low = min(self.min_step, self.max_step)
            high = max(self.min_step, self.max_step)
            step_size = low + utils.random_int(seed + 2, high - low, 0)
            next_note = self.transpose_current_note(direction * step_size)

            if abs(next_note - self.center_note) > max_deviation:
                if distance > 0:
                    direction = -1
                elif distance < 0:
                    direction = 1
                else:
                    direction = -1 if next_note > self.center_note else 1
                next_note = self.transpose_current_note(direction * step_size)

            self.current_note = next_note

        note = self.current_note
        if self.tension > 0 and utils.random_int(seed + 3, 99, 0) < self.tension:
            note += direction
        note = self.clamp_note(note)

        if self.active_note is not None:
            self.note_offs_out.append_value(self.active_note)

        self.note_ons_out.append_value(note, velocity=self.last_velocity)
        self.active_note = note
        self.active_off_time = ticks.ticks_add(self.transport.now, int(self.gate))

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        running = self.transport.running
        was_running = self.was_transport_running
        got_note_on = False

        if note_ons.length > 0 or note_offs.length > 0:
            got_note_on = self.update_held_notes(note_ons, note_offs)

        if got_note_on and running and was_running:
            self.last_grid_bin = self.transport.midi_tick // self.rate_ticks

        if self.held_notes.length == 0:
            self.process_active_note_off(force=True)
            self.was_transport_running = running
            return self.note_ons_out, self.note_offs_out

        self.process_active_note_off()

        if running and not was_running:
            self.last_grid_bin = -1
            if self.retrigger_mode == RETRIGGER_RETRIGGER:
                self.reset_walk()

        if running:
            current_bin = self.transport.midi_tick // self.rate_ticks

            if current_bin < self.last_grid_bin:
                self.last_grid_bin = -1

            if current_bin != self.last_grid_bin:
                self.last_grid_bin = current_bin
                self.generate_note(current_bin)

        self.was_transport_running = running
        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes.clear()
        self.active_note = None
        self.reset_walk()
        self.was_transport_running = False

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
