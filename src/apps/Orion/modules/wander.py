from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core import utils
from ..core.constants import POLYPHONY
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks


CALM_SHAPES = (
    (1, -1, 0, 1),
    (-1, 1, 0, -1),
    (1, 1, -1, 0),
    (-1, -1, 1, 0),
)

MID_SHAPES = (
    (1, 1, -1, 0),
    (1, -1, 1, -1),
    (1, 1, 1, -2),
    (-1, -1, 1, 0),
    (2, -1, -1, 0),
    (-2, 1, 1, 0),
)

WILD_SHAPES = (
    (2, -1, -1, 0),
    (-2, 1, 1, 0),
    (1, 2, -1, -2),
    (-1, -2, 1, 2),
)


class Wander(Module):
    name = "wander"
    label = "WANDR"
    help_text = "SCALE MELODY WALK"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]

        self.wander = 45          # 0-100, bigger = larger jumps
        self.range = 5            # scale steps from center
        self.density = 100        # chance to emit note
        self.tension = 0          # chance for chromatic passing offset
        self.gate = 100
        self.polyphony = 1
        self.random_seed_user = 0
        self.random_pattern_length = 0

        self.held_notes = NoteArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_offs = NoteArray(times=True)

        self.center_note = 60
        self.current_offset = 0   # scale steps relative to center_note
        self.last_move = 0
        self.last_velocity = 127
        self.walk_state = 0
        self.current_shape = CALM_SHAPES[0]
        self.shape_step = 0
        self.shape_sign = 1

        self.last_grid_bin = -1
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []

        parms.append(Parms.Parm(
            name="rate", label="RATE", default=self.rate_value,
            parm_type=Parms.RateParmType,
            help_text="STEP RATE",
            edit_callback_function=self.set_rate
        ))

        parms.append(Parms.Parm(
            name="wander", label="WNDR", default=self.wander,
            parm_type=Parms.IntParmType,
            help_text="STEP SIZE",
            minmax=(0, 100),
            bind_object=self,
            bind_attribute="wander"
        ))

        parms.append(Parms.Parm(
            name="range", label="RNG", default=self.range,
            parm_type=Parms.IntParmType,
            help_text="SCALE STEP RANGE",
            minmax=(1, 16),
            bind_object=self,
            bind_attribute="range"
        ))

        parms.append(Parms.Parm(
            name="density", label="DENS", default=self.density,
            parm_type=Parms.IntParmType,
            help_text="NOTE CHANCE",
            minmax=(0, 100),
            bind_object=self,
            bind_attribute="density"
        ))

        parms.append(Parms.Parm(
            name="tension", label="TENS", default=self.tension,
            parm_type=Parms.IntParmType,
            help_text="CHROMATIC CHANCE",
            minmax=(0, 100),
            bind_object=self,
            bind_attribute="tension"
        ))

        parms.append(Parms.Parm(
            name="gate", label="GATE", default=self.gate,
            parm_type=Parms.FloatParmType,
            help_text="NOTE GATE MS",
            increment=5,
            bind_object=self,
            bind_attribute="gate"
        ))

        parms.append(Parms.Parm(
            name="polyphony", label="POLY", default=self.polyphony,
            parm_type=Parms.IntParmType,
            help_text="MAX HELD NOTES",
            minmax=(1, POLYPHONY),
            bind_object=self,
            bind_attribute="polyphony"
        ))

        parms.append(Parms.Parm(
            name="random_pattern_length", label="RND LEN",
            default=self.random_pattern_length,
            parm_type=Parms.IntParmType,
            minmax=(0, 128),
            help_text="RAND LOOP LEN",
            bind_object=self,
            bind_attribute="random_pattern_length"
        ))

        parms.append(Parms.Parm(
            name="rand_seed", label="SEED", default=self.random_seed_user,
            parm_type=Parms.IntParmType,
            help_text="RAND SEED",
            minmax=(0, 10000),
            bind_object=self,
            bind_attribute="random_seed_user"
        ))

        return parms

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def rate_to_midi_ticks(self):
        return self.rate_ticks

    def clamp_note(self, note):
        if note < 0:
            return 0
        if note > 127:
            return 127
        return note

    def update_held_notes(self, note_ons, note_offs):
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            self.held_notes.append_value(note)

            self.center_note = note
            self.current_offset = 0
            self.last_move = 0
            self.shape_step = len(self.current_shape)

            if note_ons.velocities is not None:
                self.last_velocity = note_ons.velocities[i]
            else:
                self.last_velocity = 127

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)

        if self.held_notes.length > 0 and not self.held_notes.contains(self.center_note):
            # If center note was released, use newest remaining-ish fallback.
            self.center_note = self.held_notes.notes[self.held_notes.length - 1]
            self.current_offset = 0
            self.last_move = 0
            self.shape_step = len(self.current_shape)

    def pick_shape(self, seed):
        if self.wander < 35:
            shapes = CALM_SHAPES
        elif self.wander < 70:
            shapes = MID_SHAPES
        else:
            shapes = WILD_SHAPES

        return shapes[utils.random_int(seed, len(shapes)-1, 0)]

    def update_shape_direction(self):
        if self.current_offset >= self.range:
            self.shape_sign = -1
        elif self.current_offset <= -self.range:
            self.shape_sign = 1

    def choose_move(self, seed):
        self.update_shape_direction()

        if self.shape_step >= len(self.current_shape):
            self.current_shape = self.pick_shape(seed)
            self.shape_step = 0
            self.update_shape_direction()

        move = self.current_shape[self.shape_step] * self.shape_sign
        self.shape_step += 1
        return move

    def offset_to_note(self, offset):
        scale = music.SCALES[self.module_helper.state.scale]

        # Treat offset as scale-aware semitone/degree movement using your existing transpose behavior.
        note = music.transpose(
            self.center_note,
            offset,
            0,
            True,
            self.state.key,
            scale
        )

        return self.clamp_note(note)

    def generate_note(self):
        midi_tick = self.transport.midi_tick
        timing_tick = midi_tick // self.rate_to_midi_ticks()

        rand_step = timing_tick
        if self.random_pattern_length != 0:
            rand_step = timing_tick % self.random_pattern_length

        seed = self.random_seed_user + rand_step

        # Density skip.
        if utils.random_int(seed + 10, 99, 0) >= self.density:
            return

        move = self.choose_move(seed + 20)

        self.current_offset += move

        # Safety clamp / bounce.
        if self.current_offset > self.range:
            self.current_offset = self.range - 1
            move = -1
        elif self.current_offset < -self.range:
            self.current_offset = -self.range + 1
            move = 1

        note = self.offset_to_note(self.current_offset)

        # Optional phrase-position chromatic tension.
        if (
            self.tension > 0
            and self.shape_step == 2
            and utils.random_int(seed + 30, 99, 0) < self.tension
        ):
            if move * self.shape_sign >= 0:
                note += 1
            else:
                note -= 1
            note = self.clamp_note(note)

        now = self.transport.now
        off_time = ticks.ticks_add(now, self.gate)

        self.enforce_polyphony_limit()
        self.note_ons_out.append_value(note, velocity=self.last_velocity)
        self.scheduled_offs.append_value(note, time=off_time)

        self.last_move = move

    def enforce_polyphony_limit(self):
        limit = self.polyphony
        if limit < 1:
            limit = 1

        while self.scheduled_offs.length >= limit:
            self.note_offs_out.append_value(self.scheduled_offs.notes[0])
            self.scheduled_offs.remove_index(0)

    def process_note_offs(self, force_all=False):
        current = self.transport.now

        i = 0
        while i < self.scheduled_offs.length:
            note = self.scheduled_offs.notes[i]
            off_time = self.scheduled_offs.times[i]

            if force_all or ticks.ticks_less(off_time, current):
                self.note_offs_out.append_value(note)
                self.scheduled_offs.remove_index(i)
            else:
                i += 1

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        running = self.transport.running

        if note_ons.length > 0 or note_offs.length > 0:
            self.update_held_notes(note_ons, note_offs)

        if self.walk_state == 1 and self.held_notes.length < 1:
            self.walk_state = 0
            self.current_offset = 0
            self.last_move = 0
            self.shape_step = len(self.current_shape)
            self.process_note_offs(force_all=True)

        if self.walk_state == 0 and self.held_notes.length > 0:
            self.walk_state = 1
            self.current_offset = 0
            self.last_move = 0
            self.shape_step = len(self.current_shape)

        if running and not self.was_transport_running:
            self.last_grid_bin = -1

        if running and self.walk_state == 1:
            interval = self.rate_to_midi_ticks()
            current_bin = self.transport.midi_tick // interval

            if current_bin < self.last_grid_bin:
                self.last_grid_bin = -1

            if current_bin != self.last_grid_bin:
                self.last_grid_bin = current_bin
                self.generate_note()

        self.process_note_offs()
        self.was_transport_running = running

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.scheduled_offs.clear()
        self.held_notes.clear()

        self.walk_state = 0
        self.current_offset = 0
        self.last_move = 0
        self.shape_step = len(self.current_shape)
        self.last_grid_bin = -1
        self.was_transport_running = False

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
        self.scheduled_offs = None
