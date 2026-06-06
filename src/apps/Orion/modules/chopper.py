from ..core import music
from ..core.module import Module
from ..core import parms as Parms
from ..core import utils
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray


RETRIGGER_OPTIONS = (
    "STBL",
    "CONT",
    "RTRG",
)


class Chopper(Module):
    name = "chopper"
    label = "CHOPPR"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.chop = 0.5
        self.pattern_length = 16
        self.random_seed_user = 0
        self.retrigger_mode = 0
        self.held_notes = NoteOnArray()
        self.active_notes = NoteArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.blocked = False
        self.step_index = 0
        self.last_grid_bin = -1
        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        return [
            Parms.Parm(name="rate", label="RATE", default=self.rate_value,
                       parm_type=Parms.RateParmType, edit_callback_function=self.set_rate),
            Parms.Parm(name="chop", label="CHOP", default=self.chop,
                       parm_type=Parms.PercentParmType, increment=0.05,
                       bind_object=self, bind_attribute="chop"),
            Parms.Parm(name="pattern_length", label="PTLEN", default=self.pattern_length,
                       parm_type=Parms.IntParmType, minmax=(1, 128),
                       bind_object=self, bind_attribute="pattern_length"),
            Parms.Parm(name="rand_seed", label="SEED", default=self.random_seed_user,
                       parm_type=Parms.IntParmType, minmax=(0, 10000),
                       bind_object=self, bind_attribute="random_seed_user"),
            Parms.Parm(name="retrigger_mode", label="RTRG", default=self.retrigger_mode,
                       parm_type=Parms.EnumParmType, options=RETRIGGER_OPTIONS,
                       bind_object=self, bind_attribute="retrigger_mode"),
        ]

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        had_notes = self.held_notes.length > 0

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.held_notes.remove_value_first(note)
            if self.active_notes.contains(note):
                self.active_notes.remove_value_first(note)
                self.note_offs_out.append_value(note)

        if note_ons.length > 0 and not had_notes and self.retrigger_mode == 2:
            self.step_index = 0
            self.last_grid_bin = -1

        if self.transport.running:
            current_bin = self.transport.midi_tick // self.rate_ticks
            if current_bin < self.last_grid_bin:
                self.last_grid_bin = -1
            if current_bin != self.last_grid_bin and (self.held_notes.length > 0 or note_ons.length > 0):
                self.last_grid_bin = current_bin
                if self.retrigger_mode == 0:
                    step = current_bin
                else:
                    step = self.step_index
                    self.step_index += 1
                pattern_length = self.pattern_length
                step = step % pattern_length
                blocked_steps = int((self.chop * pattern_length) + 0.5)
                rank = 0
                step_value = utils.random_int(self.random_seed_user + step, 65535, 0)
                for p in range(pattern_length):
                    value = utils.random_int(self.random_seed_user + p, 65535, 0)
                    if value < step_value or (value == step_value and p < step):
                        rank += 1
                self.blocked = rank < blocked_steps
                if self.blocked:
                    for i in range(self.active_notes.length):
                        self.note_offs_out.append_value(self.active_notes.notes[i])
                    self.active_notes.clear()
                else:
                    for i in range(self.held_notes.length):
                        note = self.held_notes.notes[i]
                        if not self.active_notes.contains(note):
                            self.active_notes.append_value(note)
                            self.note_ons_out.append_value(note, velocity=self.held_notes.velocities[i])
            elif self.retrigger_mode != 0 and self.held_notes.length == 0 and note_ons.length == 0:
                self.last_grid_bin = current_bin

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            if self.held_notes.length >= self.held_notes.max_length and not self.held_notes.contains(note):
                old_note = self.held_notes.notes[0]
                if self.active_notes.contains(old_note):
                    self.active_notes.remove_value_first(old_note)
                    self.note_offs_out.append_value(old_note)
                self.held_notes.remove_index(0)

            if self.held_notes.contains(note):
                for h in range(self.held_notes.length):
                    if self.held_notes.notes[h] == note:
                        self.held_notes.velocities[h] = velocity
                        break
            else:
                self.held_notes.append_value(note, velocity=velocity)

            if not self.blocked:
                if self.active_notes.contains(note):
                    self.active_notes.remove_value_first(note)
                    self.note_offs_out.append_value(note)
                self.active_notes.append_value(note)
                self.note_ons_out.append_value(note, velocity=velocity)

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes.clear()
        self.active_notes.clear()
        self.blocked = False
        self.step_index = 0
        self.last_grid_bin = -1

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
        self.active_notes = None
        self.retrigger_mode = None
