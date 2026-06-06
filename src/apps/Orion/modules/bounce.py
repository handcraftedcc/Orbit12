from ..core import music
from ..core.module import Module
from ..core import parms as Parms
from ..core import utils
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks

MODCURVEOPTIONS = (
    "LIN",
    "CUBIC",
    "RAND",
)

MODEOPTIONS = (
    "MS",
    "RATE",
)

class Bounce(Module):
    name = "bounce"
    label = "BOUNCE"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport
        self.bounces = 3
        self.interval_mode = 0
        self.interval_ms = 40
        self.interval_rate_value = 8
        self.interval_rate = music.RATE_MIDI_TICKS[self.interval_rate_value]
        self.interval_mod = 0 # 0 means steady - 1 means goes to 0 by bounces+1 bounces
        self.velocity_mod = 1
        self.mod_curve = 0
        self.gate = 100
        self.gate_mod = 0

        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.bounce_notes = NoteArray(velocities=True, times=True)
        self.bounce_bounces = NoteArray()
        self.scheduled_offs = NoteArray(times=True)
        self.popped_ids = NoteArray()
        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        return [
            Parms.Parm(name="bounces", label="BOUNCE", default=self.bounces, parm_type=Parms.IntParmType,
                       minmax=(0, 64), increment=1, bind_object=self, bind_attribute="bounces"),
            Parms.Parm(name="interval_mode", label="MODE", default=self.interval_mode,
                       parm_type=Parms.EnumParmType, options=MODEOPTIONS,
                       bind_object=self, bind_attribute="interval_mode"),
            Parms.Parm(name="interval_ms", label="INTMS", default=self.interval_ms, parm_type=Parms.IntParmType,
                       increment=5, bind_object=self, bind_attribute="interval_ms"),
            Parms.Parm(name="interval_rate", label="INTRT", default=self.interval_rate_value,
                       parm_type=Parms.RateParmType, edit_callback_function=self.set_interval_rate),
            Parms.Parm(name="gate", label="GATE", default=self.gate, parm_type=Parms.FloatParmType,
                       increment=5, bind_object=self, bind_attribute="gate"),
            Parms.Parm(name="interval_mod", label="INTMOD", default=self.interval_mod, parm_type=Parms.PercentParmType,
                       increment=0.05, bind_object=self, bind_attribute="interval_mod"),
            Parms.Parm(name="velocity_mod", label="VELMOD", default=self.velocity_mod, parm_type=Parms.PercentParmType,
                       increment=0.05, bind_object=self, bind_attribute="velocity_mod"),
            Parms.Parm(name="gate_mod", label="GTEMOD", default=self.gate_mod, parm_type=Parms.PercentParmType,
                       increment=0.05, bind_object=self, bind_attribute="gate_mod"),
            Parms.Parm(name="mod_curve", label="MODCRV", default=self.mod_curve, parm_type=Parms.EnumParmType,
                       options=MODCURVEOPTIONS, bind_object=self, bind_attribute="mod_curve"),
        ]

    def set_interval_rate(self, value):
        self.interval_rate_value = value
        self.interval_rate = music.RATE_MIDI_TICKS[value]
        return self.interval_rate

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        current = self.transport.now
        current_tick = self.transport.midi_tick

        self.popped_ids.clear()
        for i in range(self.scheduled_offs.length):
            if ticks.ticks_less(self.scheduled_offs.times[i], current):
                self.note_offs_out.append_value(self.scheduled_offs.notes[i])
                self.popped_ids.append_value(i)
        for i in range(self.popped_ids.length - 1, -1, -1):
            self.scheduled_offs.remove_index(self.popped_ids.notes[i])

        for i in range(note_ons.length):
            if self.bounce_bounces.length >= self.bounce_bounces.max_length:
                if self.note_offs_out.length >= self.note_offs_out.max_length:
                    continue
                self.note_offs_out.append_value(self.bounce_notes.notes[0])
                self.bounce_notes.remove_index(0)
                self.bounce_bounces.remove_index(0)

            note = note_ons.notes[i]
            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            self.note_ons_out.append_value(note, velocity=velocity)
            self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, int(self.gate)))

            if self.bounces > 0:
                if self.interval_mode == 1:
                    time = current_tick + self.interval_rate
                else:
                    time = ticks.ticks_add(current, int(self.interval_ms))
                self.bounce_bounces.append_value(self.bounces)
                self.bounce_notes.append_value(note, velocity=velocity, time=time)

        i = 0
        while i < self.bounce_bounces.length:
            if ((self.interval_mode == 1 and self.bounce_notes.times[i] <= current_tick) or
                    (self.interval_mode == 0 and ticks.ticks_less(self.bounce_notes.times[i], current))):
                note = self.bounce_notes.notes[i]
                remaining = self.bounce_bounces.notes[i]
                progress = (self.bounces - remaining + 1) / (self.bounces + 1)
                if progress < 0:
                    progress = 0
                elif progress > 1:
                    progress = 1
                if self.mod_curve == 1:
                    progress = progress * progress * progress
                elif self.mod_curve == 2:
                    progress = utils.random_int(self.transport.midi_tick + note + remaining, 100, 0) / 100
                velocity = int(self.bounce_notes.velocities[i] * (1 - (self.velocity_mod * progress)))
                gate = int(self.gate * (1 - (self.gate_mod * progress)))
                if velocity < 1:
                    velocity = 1
                if gate < 1:
                    gate = 1
                self.note_ons_out.append_value(note, velocity=velocity)
                self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, gate))
                remaining -= 1
                if remaining <= 0:
                    self.bounce_notes.remove_index(i)
                    self.bounce_bounces.remove_index(i)
                    continue
                self.bounce_bounces.notes[i] = remaining
                if self.interval_mode == 1:
                    interval = int((self.interval_rate * (0.01 ** (self.interval_mod * progress))) + 0.5)
                else:
                    interval = int(self.interval_ms * (0.01 ** (self.interval_mod * progress)))
                if interval < 1:
                    interval = 1
                if self.interval_mode == 1:
                    self.bounce_notes.times[i] = current_tick + interval
                else:
                    self.bounce_notes.times[i] = ticks.ticks_add(current, interval)
            i += 1

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.bounce_notes.clear()
        self.bounce_bounces.clear()
        self.scheduled_offs.clear()
        self.popped_ids.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.bounce_notes = None
        self.bounce_bounces = None
        self.scheduled_offs = None
        self.popped_ids = None
